#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include "string.h"

#include <SDL2/SDL_scancode.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"
#include "timers.h"

#include "TUM_Ball.h"
#include "TUM_Draw.h"
#include "TUM_Font.h"
#include "TUM_Event.h"
#include "TUM_Sound.h"
#include "TUM_Utils.h"
#include "TUM_FreeRTOS_Utils.h"
#include "TUM_Print.h"

#include "AsyncIO.h"

#include "text.h"
#include "objects.h"
#include "pvp.h"

#define mainGENERIC_PRIORITY (tskIDLE_PRIORITY)
#define mainGENERIC_STACK_SIZE ((unsigned short)2560)
#define STACK_SIZE mainGENERIC_STACK_SIZE * 2

#define UNITARY_QUEUE_LENGHT 1

#define STATE_COUNT 3

#define MENU 0
#define GAME 1
#define PAUSE 2

#define NEXT_TASK 0
#define PREV_TASK 1

#define STARTING_STATE MENU

#define STATE_DEBOUNCE_DELAY 300

#define INCREMENT_MONSTER_DELAY 4
#define DECREMENT_MONSTER_DELAY 2

#define KEYCODE(CHAR) SDL_SCANCODE_##CHAR
#define BACKGROUND_COLOUR Black

#define GREEN_LINE_Y SCREEN_HEIGHT - 38
#define TOP_LINE_Y 75

#ifdef TRACE_FUNCTIONS
#include "tracer.h"
#endif

const unsigned char next_state_signal = NEXT_TASK;
const unsigned char prev_state_signal = PREV_TASK;

static TaskHandle_t StateMachine = NULL;
static TaskHandle_t BufferSwap = NULL;
static TaskHandle_t MenuDrawer = NULL;
static TaskHandle_t GameLogic = NULL;
static TaskHandle_t GameDrawer = NULL;
static TaskHandle_t EnemyBulletShooter = NULL;
static TaskHandle_t MonsterMover = NULL;
static TaskHandle_t PauseDrawer = NULL;
static TaskHandle_t CheckInputSetScore = NULL;

TimerHandle_t xMothershipTimer;

static StaticTask_t GameDrawerBuffer;
static StackType_t xStack[STACK_SIZE];

static QueueHandle_t StateChangeQueue = NULL;
static QueueHandle_t CurrentStateQueue = NULL;
QueueHandle_t BulletQueue = NULL;
QueueHandle_t ColisionQueue = NULL;
QueueHandle_t MonsterDelayQueue = NULL;
QueueHandle_t TimerStartingQueue = NULL;
static QueueHandle_t LevelQueue = NULL;

static image_handle_t spaceship_image = NULL;
static image_handle_t monster_image[3] = {NULL};
static image_handle_t colision_image[2] = {NULL};
static image_handle_t mothership_image = NULL;
static image_handle_t bunker_image[6] = {NULL};

static spritesheet_handle_t monster_spritesheet[3] = {NULL};

static SemaphoreHandle_t DrawSignal = NULL;
static SemaphoreHandle_t ScreenLock = NULL;

aIO_handle_t UDP_receive_handle = NULL;
aIO_handle_t UDP_transmit_handle = NULL;

typedef struct buttons_buffer {
	unsigned char buttons[SDL_NUM_SCANCODES];
	SemaphoreHandle_t lock;
} buttons_buffer_t;

static buttons_buffer_t buttons = { 0 };

player_t my_player = { 0 };

saved_values_t saved = { 0 };

spaceship_t my_spaceship = { 0 };

monster_grid_t my_monsters = { 0 };

mothership_t my_mothership = { 0 };

bunker_grid_t my_bunkers = { 0 };

void checkDraw(unsigned char status, const char *msg)
{
	if (status) {
		if (msg)
			fprints(stderr, "[ERROR] %s, %s\n", msg,
				tumGetErrorMessage());
		else {
			fprints(stderr, "[ERROR] %s\n", tumGetErrorMessage());
		}
	}
}

void vCheckCoinInput(void)
{
    static int debounce_flag = 0;

    if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
		if (buttons.buttons[KEYCODE(C)]) {
			if (!debounce_flag) {
                debounce_flag = 1;
                vInsertCoin();
                prints("Coin Inserted.\n");
            }
		} else {
            debounce_flag = 0;
        }
		xSemaphoreGive(buttons.lock);
	}
}

void vResetGameBoard(void)
{
    int current_state;

    vResetBulletQueue();
    vResetColisionQueue();
    vResetMonsters();
    vResetMonsterDelay();
    vResetSpaceship();
    vResetBunkers();

    xQueuePeek(CurrentStateQueue, &current_state, portMAX_DELAY);
    if (my_player.n_players == 1 && current_state == GAME) {
        vResetMothership();
        vKillMothership();
        xTimerReset(xMothershipTimer, portMAX_DELAY);
    } else if (my_player.n_players == 2)
        vSetUpMothershipPVP();
}

void xGetButtonInput(void)
{
	if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
		xQueueReceive(buttonInputQueue, &buttons.buttons, 0);
		xSemaphoreGive(buttons.lock);
	}
}

void vComputeLevel(void)
{
    int level, monster_delay;

    xQueuePeek(MonsterDelayQueue, &monster_delay, portMAX_DELAY);
    //if the subtraction is negative then the level should be positive, since is harder
    level = -1 * (monster_delay - ORIGINAL_MONSTER_DELAY);
    //increment is used when cheating, decrement when level is beat
    //different values so to compute level correctly different division are to be used
    if (level < 0)
        level = level / INCREMENT_MONSTER_DELAY;
    else
        level = level / DECREMENT_MONSTER_DELAY;
    xQueueOverwrite(LevelQueue, &level);
}

void vPauseOrUnpauseGame(void)
{
    unsigned char current_state;

    if (xQueuePeek(CurrentStateQueue, &current_state, 0) ==
		pdTRUE) {
            if (current_state == GAME) {
                xQueueSend(StateChangeQueue, &next_state_signal, 0);
            } else if (current_state == PAUSE) {
                xQueueSend(StateChangeQueue, &prev_state_signal, 0);
            }
	}
}

static int vCheckPauseInput(void)
{
	if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
		if (buttons.buttons[KEYCODE(P)]) {
			buttons.buttons[KEYCODE(P)] = 0;
			if (StateChangeQueue) {
				xSemaphoreGive(buttons.lock);
                vPauseOrUnpauseGame();
				return 1;
			}
			xSemaphoreGive(buttons.lock);
			return 0;
		}
		xSemaphoreGive(buttons.lock);
	}

	return 0;
}

void vPlayOrQuitGame(void)
{
    unsigned char current_state;

    if (xQueuePeek(CurrentStateQueue, &current_state, 0) ==
			            pdTRUE) {
        if (current_state == MENU && my_player.credits > 0) {
            vUseCoin();
            xQueueSend(StateChangeQueue, &next_state_signal, 0);
        } else if (current_state == GAME) {
            xQueueSend(StateChangeQueue, &prev_state_signal, 0);
        }
	}
}

static int vCheckStateInput(void)
{
	if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
		if (buttons.buttons[KEYCODE(M)]) {
			buttons.buttons[KEYCODE(M)] = 0;
			if (StateChangeQueue) {
				xSemaphoreGive(buttons.lock);
                vPlayOrQuitGame();
				return 1;
			}
			xSemaphoreGive(buttons.lock);
			return 0;
		}
		xSemaphoreGive(buttons.lock);
	}
	return 0;
}

//suspends all tasks except 
//screendrawer task and state machine tast
void vSuspendAllTasks()
{
	if (MenuDrawer) {
		vTaskSuspend(MenuDrawer);
	}
	if (GameDrawer) {
		vTaskSuspend(GameDrawer);
	}
	if (GameLogic) {
		vTaskSuspend(GameLogic);
	}
    if (EnemyBulletShooter) {
		vTaskSuspend(EnemyBulletShooter);
    }
    if (MonsterMover) {
		vTaskSuspend(MonsterMover);
    }
    if (PauseDrawer) {
		vTaskSuspend(PauseDrawer);
    }
    if (CheckInputSetScore) {
		vTaskSuspend(CheckInputSetScore);
    }
}

/*
 * Changes the state, either forwards of backwards
 */
void changeState(volatile unsigned char *state, unsigned char forwards)
{
	switch (forwards) {
	case NEXT_TASK:
		if (*state == STATE_COUNT - 1) {
			*state = 0;
		} else {
			(*state)++;
		}
		break;
	case PREV_TASK:
		if (*state == 0) {
			*state = STATE_COUNT - 1;
		} else {
			(*state)--;
		}
		break;
	default:
		break;
	}
}

void vSwitchToMenu(unsigned char prev_state)
{
    if (prev_state == GAME) {
        vResetGameBoard();
        vResetPlayer();
        if (my_player.n_players == 1)
            xTimerStop(xMothershipTimer, portMAX_DELAY);
            prints("Match exited.\n");
    }
	if (MenuDrawer) {
		vTaskResume(MenuDrawer);
	}
}

void vResumeGameTasks(void)
{
    if (GameDrawer) {
		vTaskResume(GameDrawer);
	}
	if (GameLogic) {
		vTaskResume(GameLogic);
    }
    if (EnemyBulletShooter) {
		vTaskResume(EnemyBulletShooter);
    }
    if (MonsterMover) {
		vTaskResume(MonsterMover);
    }
}

void vSwitchToGame(unsigned char prev_state,
         unsigned char current_state, TickType_t timer_elapsed)
{
    if (prev_state == MENU || prev_state == current_state) {
        vComputeLevel();
        if (my_player.n_players == 1) {
            vKillMothership();
            xTimerReset(xMothershipTimer, portMAX_DELAY);
        }
        if (my_player.n_players == 2) {
            vSetUpMothershipPVP();
            prints("2 Players selected.\n");
        }
        prints("Match started! Good luck and Have fun!\n");
    }
    if (prev_state == PAUSE) {
        if (my_player.n_players == 1)
            xTimerChangePeriod(xMothershipTimer, ORIGINAL_TIMER - timer_elapsed, portMAX_DELAY);
            prints("Game unpaused.\n");
    }
    vResumeGameTasks();
}

void vSwitchToPause(unsigned char prev_state,
             TickType_t *timer_starting, TickType_t *timer_elapsed)
{
    if (prev_state == GAME) {
        if (my_player.n_players == 1) {
            xTimerStop(xMothershipTimer, portMAX_DELAY);
            *timer_starting = xQueuePeek(TimerStartingQueue, timer_starting, portMAX_DELAY);
            *timer_elapsed = xTaskGetTickCount() - *timer_starting;
        }
        prints("Game paused.\n");
    }
    if (PauseDrawer) {
		vTaskResume(PauseDrawer);
    }
}

void basicSequentialStateMachine(void *pvParameters)
{
	unsigned char current_state = STARTING_STATE;//Default state
    unsigned char prev_state = STARTING_STATE;
	unsigned char state_changed =
		1; // Only re-evaluate state if it has changed
	unsigned char input = 0;

	const int state_change_period = STATE_DEBOUNCE_DELAY;

	TickType_t last_change, timer_elapsed, timer_starting;

    last_change = xTaskGetTickCount();

    xQueueOverwrite(CurrentStateQueue,
							&current_state);

	while (1) {
		if (state_changed) {
			goto initial_state;
		}

		// Handle state machine input
		if (StateChangeQueue)
			if (xQueueReceive(StateChangeQueue, &input,
					  portMAX_DELAY) == pdTRUE)
				if (xTaskGetTickCount() - last_change >
				    state_change_period) {
                    prev_state = current_state;
					changeState(&current_state, input);
					xQueueOverwrite(CurrentStateQueue,
							&current_state);
					state_changed = 1;
					last_change = xTaskGetTickCount();
				}

	initial_state:
		// Handle current state
		if (state_changed) {
			vSuspendAllTasks();
			switch (current_state) {
			case MENU:
                vSwitchToMenu(prev_state);
				break;
			case GAME:
                vSwitchToGame(prev_state, current_state, timer_elapsed);
				break;
            case PAUSE:
                vSwitchToPause(prev_state, &timer_starting, &timer_elapsed);
                break;
			default:
				break;
			}
			state_changed = 0;
		}
	}
}

void vSwapBuffers(void *pvParameters)
{
	TickType_t xLastWakeTime;
	xLastWakeTime = xTaskGetTickCount();
	const TickType_t frameratePeriod = 20;

	tumDrawBindThread(); // Setup Rendering handle with correct GL context

	while (1) {
		xSemaphoreTake(ScreenLock, portMAX_DELAY);
		tumDrawUpdateScreen();
		tumEventFetchEvents(FETCH_EVENT_BLOCK);
		xSemaphoreGive(DrawSignal);
		xSemaphoreGive(ScreenLock);
		vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(frameratePeriod));
	}
}

int vCheckButtonInput(int key)
{
	if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
		if (buttons.buttons[key]) {
			xSemaphoreGive(buttons.lock);
			switch (key) {
				case KEYCODE(A):
                    vMoveSpaceship(RIGHT_TO_LEFT);
					break;
				case KEYCODE(D):
                    vMoveSpaceship(LEFT_TO_RIGHT);
					break;
				default:
					 break;
			}
			return 0;
		}
		xSemaphoreGive(buttons.lock);
	}
	return 0;
}

#define UPPER_TEXT_YLOCATION 10
#define LOWER_TEXT_YLOCATION SCREEN_HEIGHT - DEFAULT_FONT_SIZE - 20

void vDrawScores(void)
{
    vDrawText("SCORE<1>", 10, UPPER_TEXT_YLOCATION, NOT_CENTERING);
    vDrawNumber(my_player.score1, 45, UPPER_TEXT_YLOCATION + DEFAULT_FONT_SIZE * 1.3, 4);
    vDrawText("HI-SCORE", SCREEN_WIDTH / 3 + 10, UPPER_TEXT_YLOCATION, NOT_CENTERING);
    vDrawNumber(my_player.highscore, SCREEN_WIDTH / 3 + 25, UPPER_TEXT_YLOCATION + DEFAULT_FONT_SIZE * 1.3, 4);
    vDrawText("SCORE<2>", SCREEN_WIDTH * 2 / 3 + 10, UPPER_TEXT_YLOCATION, NOT_CENTERING);
    if (my_player.n_players == 2) {
        vDrawNumber(my_player.score2, SCREEN_WIDTH * 2 / 3 + 25, UPPER_TEXT_YLOCATION + DEFAULT_FONT_SIZE * 1.3, 4);
    }
}

void vDrawCredit(void)
{
    vDrawText("CREDIT", SCREEN_WIDTH * 2 / 3 - 20, LOWER_TEXT_YLOCATION, NOT_CENTERING);
    vDrawNumber(my_player.credits, SCREEN_WIDTH - 55, LOWER_TEXT_YLOCATION, 2);
}

void vDrawLevel(void)
{
    int level;

    vDrawText("LEVEL:", SCREEN_WIDTH / 3 - 15, LOWER_TEXT_YLOCATION, NOT_CENTERING);
    xQueuePeek(LevelQueue, &level, portMAX_DELAY);
    if (level < 0) {
        vDrawText("- ", SCREEN_WIDTH / 3 + 80, LOWER_TEXT_YLOCATION, NOT_CENTERING);
        vDrawNumber(level, SCREEN_WIDTH / 3 + 90, LOWER_TEXT_YLOCATION, 1);
    } else {
        vDrawNumber(level, SCREEN_WIDTH / 3 + 80, LOWER_TEXT_YLOCATION, 1);
    }
}

void vCheckGameInput(void)
{
	vCheckStateInput();
	vCheckButtonInput(KEYCODE(A));
	vCheckButtonInput(KEYCODE(D));
    vCheckPauseInput();
}

void vDrawFirstScreen(void)
{
    vDrawText("INSERT  [C]OIN", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 3, CENTERING);
    vDrawText("<[1] OR [2] PLAYERS>", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2, CENTERING);
    if (my_player.n_players == 1) {
        vDrawText("1 PLAYER", SCREEN_WIDTH / 2, SCREEN_HEIGHT * 2 / 3, CENTERING);
    }
    if (my_player.n_players == 2) {
        vDrawText("2 PLAYERS", SCREEN_WIDTH / 2, SCREEN_HEIGHT * 2 / 3, CENTERING);
    }
}

void vDrawSecondScreen(void)
{
    vDrawText("SPACE INVADERS", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 4 - 30, CENTERING);
    vDrawText("[M]: PLAY/QUIT", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 4  + DEFAULT_FONT_SIZE * 1.5 - 30, CENTERING);
    vDrawText("[P]: PAUSE/UNPAUSE", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 4 + DEFAULT_FONT_SIZE * 3 - 30, CENTERING);

    vDrawText("SCORE ADVANCE TABLE", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 3 + DEFAULT_FONT_SIZE * 1.5 + 10, CENTERING);
    checkDraw(tumDrawLoadedImage(mothership_image, SCREEN_WIDTH / 5 - 5, SCREEN_HEIGHT / 3 + DEFAULT_FONT_SIZE * 3 + 10), __FUNCTION__);
    vDrawText("=? MYSTERY", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 3 + DEFAULT_FONT_SIZE * 3 + 10, CENTERING);
    checkDraw(tumDrawSprite(monster_spritesheet[0], 0, 0, SCREEN_WIDTH / 4 + 3, SCREEN_HEIGHT / 3 + DEFAULT_FONT_SIZE * 4.5 + 10), __FUNCTION__);
    vDrawText("=30 POINTS", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 3 + DEFAULT_FONT_SIZE * 4.5 + 10, CENTERING);
    checkDraw(tumDrawSprite(monster_spritesheet[1], 0, 0, SCREEN_WIDTH / 4, SCREEN_HEIGHT / 3 + DEFAULT_FONT_SIZE * 6 + 10), __FUNCTION__);
    vDrawText("=20 POINTS", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 3 + DEFAULT_FONT_SIZE * 6 + 10, CENTERING);
    checkDraw(tumDrawSprite(monster_spritesheet[2], 0, 0, SCREEN_WIDTH / 4, SCREEN_HEIGHT / 3 + DEFAULT_FONT_SIZE * 7.5 + 10), __FUNCTION__);
    vDrawText("=10 POINTS", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 3 + DEFAULT_FONT_SIZE * 7.5 + 10, CENTERING);

    vDrawText("[I]NFINITE LIVES", SCREEN_WIDTH / 2, SCREEN_HEIGHT * 3 / 4, CENTERING);
    vDrawText("STARTING [S]CORE", SCREEN_WIDTH / 2, SCREEN_HEIGHT * 3 / 4 + DEFAULT_FONT_SIZE * 1.5, CENTERING);
    vDrawText("STARTING [L]EVEL", SCREEN_WIDTH / 2, SCREEN_HEIGHT * 3 / 4 + DEFAULT_FONT_SIZE * 3, CENTERING);
}

void vDrawMenuText(void)
{
    vDrawScores();
    if (my_player.credits && my_player.n_players) {
        vDrawSecondScreen();
    } else {
        vDrawFirstScreen();
    }
    vDrawCredit();
}

void vSetCheat1(void)
{
    xSemaphoreTake(my_player.lock, portMAX_DELAY);
    if (my_player.n_lives <= 3) {
        my_player.n_lives = 9999;
        prints("You now have infinite lifes!\n");
        xSemaphoreGive(my_player.lock);
        return;
    }
    if (my_player.n_lives > 3) {
        my_player.n_lives = INITIAL_LIVES;
        prints("You now have standard ammount of lives!\n");
        xSemaphoreGive(my_player.lock);
        return;
    }
    xSemaphoreGive(my_player.lock);
    return;
}

void vCheckInputSetScore(void *pvParameters)
{
    int i, digit;
    char text[10] = {'0'};
    char digit_in_text;
    vTaskDelay(100);

    while(1) {
        tumEventFetchEvents(FETCH_EVENT_BLOCK |
					FETCH_EVENT_NO_GL_CHECK);
        xGetButtonInput();
        if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
            for (i = 30; i < 40; i++) {//scans scancodes 0 to 9
                if (buttons.buttons[i]) {
                    buttons.buttons[i] = 0;
                    digit = i - 29;//scancode 30 is equal to number 1 so we remove 29
                    if (digit == 10)//the scancode for button 0 after
                        digit = 0;//the number 9 so we have to decrement 10
                    prints("%d\n", digit);
                    digit_in_text = digit + '0';//converts int to char
                    sprintf(text, "%s%c", text, digit_in_text);//concatenates the pressed number
                }
            }
            if (buttons.buttons[SDL_SCANCODE_RETURN]) {
                xSemaphoreTake(my_player.lock, portMAX_DELAY);
                my_player.score1 = atoi(text);
                xSemaphoreGive(my_player.lock);
                xSemaphoreGive(buttons.lock);
                vTaskResume(MenuDrawer);
                vTaskSuspend(CheckInputSetScore);
            }
            xSemaphoreGive(buttons.lock);
        }
        vTaskDelay(20);
    }
}

void vSetCheat2(void)
{
    prints("Input the starting score with keyboard and press enter.\n");
    vTaskResume(CheckInputSetScore);
    vTaskSuspend(MenuDrawer);
    prints("Starting score set\n");
}

void vSetCheat3(void)
{
    int monster_delay;

    xQueuePeek(MonsterDelayQueue, &monster_delay, portMAX_DELAY);
    monster_delay = monster_delay + INCREMENT_MONSTER_DELAY;
    //delay cannot be decreased too much or it would
    // become negative by the end of each game
    if (monster_delay > ORIGINAL_MONSTER_DELAY + 20) {
        monster_delay = ORIGINAL_MONSTER_DELAY;
        prints("Monster speed reseted.\n");
    } else {
        prints("Monster speed decreased.\n");
    }
    xQueueOverwrite(MonsterDelayQueue, &monster_delay);
}

void vCheckCheatInput(void)
{
    static int debounce_flags[3] = {0};

    if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
		if (buttons.buttons[KEYCODE(I)]) {
			if (!debounce_flags[0]) {
                debounce_flags[0] = 1;
                vSetCheat1();
            }
		} else {
            debounce_flags[0] = 0;
        }

        if (buttons.buttons[KEYCODE(S)]) {
			if (!debounce_flags[1]) {
                debounce_flags[1] = 1;
                xSemaphoreGive(buttons.lock);
                vSetCheat2();
                return;
            }
		} else {
            debounce_flags[1] = 0;
        }

        if (buttons.buttons[KEYCODE(L)]) {
			if (!debounce_flags[2]) {
                debounce_flags[2] = 1;
                vSetCheat3();
            }
		} else {
            debounce_flags[2] = 0;
        }

		xSemaphoreGive(buttons.lock);
	}
}

void vCheckPlayerSet(void)
{
    if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
		if (buttons.buttons[KEYCODE(1)]) {
			vSetPlayerNumber(1);
		}
        if (buttons.buttons[KEYCODE(2)]) {
			vSetPlayerNumber(2);
		}
		xSemaphoreGive(buttons.lock);
	}
}

void vCheckMenuInput(void)
{
    vCheckStateInput();
    vCheckCheatInput();
    vCheckCoinInput();
    vCheckPlayerSet();
}

void vMenuDrawer(void *pvParameters)
{
	while (1) {
		xSemaphoreTake(DrawSignal, portMAX_DELAY);
		tumEventFetchEvents(FETCH_EVENT_BLOCK |
					FETCH_EVENT_NO_GL_CHECK);
		xGetButtonInput();// Update global input

		xSemaphoreTake(ScreenLock, portMAX_DELAY);
		// Clear screen
		checkDraw(tumDrawClear(BACKGROUND_COLOUR),
				__FUNCTION__);
		vDrawMenuText();
		xSemaphoreGive(ScreenLock);

        vCheckMenuInput();
        vUpdateSavedValues();
	}
}

int vCheckBulletHitCeiling(bullet_t my_bullet)
{
    //bullet exceeded top limit
    if (my_bullet.y <= TOP_LINE_Y && my_bullet.type == SPACESHIP_BULLET)
        return 1;

    return 0;
}

int vCheckBulletHitMonster(bullet_t my_bullet, int i, int j)
{
    if (my_bullet.y - BULLET_HEIGHT >= my_monsters.monster[i][j].y
                && my_bullet.y <= my_monsters.monster[i][j].y
                                 + my_monsters.monster[i][j].height
                && my_bullet.x >= my_monsters.monster[i][j].x
                && my_bullet.x <= my_monsters.monster[i][j].x
                                 + my_monsters.monster[i][j].width
                && my_monsters.monster[i][j].alive
                && my_bullet.type == SPACESHIP_BULLET) {
        return 1;
    }
    return 0;
}

int vCheckBulletHitFloor(bullet_t my_bullet)
{
    if (my_bullet.y + BULLET_HEIGHT >= GREEN_LINE_Y 
                        && (my_bullet.type == MONSTER_BULLET 
                        || my_bullet.type == MOTHERSHIP_BULLET)) {
        return 1;
    }
    return 0;
}

int vCheckBulletHitSpaceship(bullet_t my_bullet)
{
    if (my_bullet.y + BULLET_HEIGHT >= my_spaceship.y
                            && my_bullet.y <= my_spaceship.y + my_spaceship.height
                            && my_bullet.x >= my_spaceship.x
                            && my_bullet.x <= my_spaceship.x + my_spaceship.width
                            && (my_bullet.type == MONSTER_BULLET
                            || my_bullet.type == MOTHERSHIP_BULLET)) {
        return 1;
    }
    return 0;
}

int vCheckBulletHitMothership(bullet_t my_bullet)
{
    if (my_bullet.y - BULLET_HEIGHT >= my_mothership.y
                            && my_bullet.y <= my_mothership.y + my_mothership.height
                            && my_bullet.x >= my_mothership.x
                            && my_bullet.x <= my_mothership.x + my_mothership.width
                            && my_mothership.alive 
                            && my_bullet.type == SPACESHIP_BULLET) {
            return 1;
    }
    return 0;
}

int vCheckBulletHitBunker(bullet_t my_bullet, int a, int i, int j)
{
    if (my_bunkers.bunker[a].component[i][j].damage < 3
                    && (my_bullet.y - BULLET_HEIGHT >= my_bunkers.bunker[a].component[i][j].y)
                    && my_bullet.y <= my_bunkers.bunker[a].component[i][j].y 
                    + my_bunkers.bunker[a].component[i][j].height
                    && my_bullet.x >= my_bunkers.bunker[a].component[i][j].x
                    && my_bullet.x <= my_bunkers.bunker[a].component[i][j].x 
                    + my_bunkers.bunker[a].component[i][j].width) {
        return 1;
    }
    return 0;
}

void vCheckBulletColision(void)
{
    int k = 0, i, j, a, n_bullets;
    bullet_t my_bullet[MAX_OBJECTS];

    while (uxQueueMessagesWaiting(BulletQueue)) {
        xQueueReceive(BulletQueue, &my_bullet[k], portMAX_DELAY);

        if (vCheckBulletHitCeiling(my_bullet[k])) {//bullet exceeded top limit
            createColision(my_bullet[k].x, my_bullet[k].y, colision_image[0]);
            goto colision_detected;
        }

        for (i = 0; i < N_ROWS; i++) {
            for (j = 0; j < N_COLUMNS; j++) {
                if (vCheckBulletHitMonster(my_bullet[k], i, j)) {
                    vUpdatePlayerScore(i, j);
                    createColision(my_monsters.monster[i][j].x 
                                     + my_monsters.monster[i][j].width / 2, 
                                    my_monsters.monster[i][j].y
                                     + my_monsters.monster[i][j].height / 2,
                                                     colision_image[1]);
                    vKillMonster(i, j);
                    tumSoundPlayUserSample("invaderkilled.wav");
                    vDecreaseMonsterDelay();
                    goto colision_detected;
                }
            }
        }

        if (vCheckBulletHitFloor(my_bullet[k])) {
            createColision(my_bullet[k].x, my_bullet[k].y + BULLET_HEIGHT,
                             colision_image[0]);
            goto colision_detected;
        }

        if (vCheckBulletHitSpaceship(my_bullet[k])) {
            vPlayerGetHit();
            if (my_bullet[k].type == MOTHERSHIP_BULLET) {
                vUpdateAIScore();
            }
            createColision(my_spaceship.x + my_spaceship.width / 2,
                     my_spaceship.y + my_spaceship.height / 2, colision_image[1]);
            tumSoundPlayUserSample("explosion.wav");
            vResetSpaceship();
            goto colision_detected;
        }

        if (vCheckBulletHitMothership(my_bullet[k])) {
            vUpdatePlayerScoreRandom();
            createColision(my_mothership.x + my_mothership.width / 2, 
                    my_mothership.y + my_mothership.height / 2, colision_image[1]);
            if (my_player.n_players == 1) {
                vResetMothership();
                vKillMothership();
            }
            goto colision_detected;
        }

        for (a = 0; a < N_BUNKERS; a++) {
            for (i = 0; i < 2; i++) {
                for (j = 0; j < 3; j++) {
                    if (vCheckBulletHitBunker(my_bullet[k], a, i, j)) {
                        vBunkerGetHit(a, i, j);
                        createColision(my_bullet[k].x, my_bullet[k].y, NULL);
                        goto colision_detected;
                    }
                }
            }
        }

        k++;// does not get here if bullet gets killed
        continue;

    colision_detected:
        continue;
    }

    n_bullets = k;
    for (k = 0; k < n_bullets; k++) {
        xQueueSend(BulletQueue, &my_bullet[k], portMAX_DELAY);
    }
}

void vIncreaseLevel(void)
{
    int monster_delay;

    xQueuePeek(MonsterDelayQueue, &monster_delay, portMAX_DELAY);
    monster_delay = monster_delay - DECREMENT_MONSTER_DELAY;
    xQueueOverwrite(MonsterDelayQueue, &monster_delay);
    vComputeLevel();
}

void vResetGame(void)
{
    vTaskSuspend(GameDrawer);
    vTaskSuspend(EnemyBulletShooter);
    vTaskDelay(pdMS_TO_TICKS(1000));
    vResetGameBoard();
    vTaskResume(GameDrawer);
    vTaskResume(EnemyBulletShooter);
}

void vCheckMonstersDead(void)
{
    int i, j, n_monster_alive = 0;

    for (i = 0; i < N_ROWS; i++) {
        for (j = 0; j < N_COLUMNS; j++) {
            if (my_monsters.monster[i][j].alive)
                n_monster_alive++;
        }
    }

    if (!n_monster_alive) {
        vResetGame();
        vIncreaseLevel();
    }
}

int vCheckMonstersInvaded(void)
{
    int i, j;

    for (i = N_ROWS - 1; i >= 0; i--) {
        for (j = 0; j < N_COLUMNS; j++) {
            if (my_monsters.monster[i][j].alive
             && my_monsters.monster[i][j].y + my_monsters.monster[i][j].height
              >= my_bunkers.bunker[1].component[0][0].y) {
                  return 1;
              }
        }
    }
    return 0;
}

void vCheckPlayerDead(void)
{
    if (!my_player.n_lives || vCheckMonstersInvaded()) {
        vResetPlayer();
        vResetGame();
    }
}

void vCheckMothershipDifficultyChange(void)
{
    if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
		if (buttons.buttons[KEYCODE(1)]) {
			buttons.buttons[KEYCODE(1)] = 0;
            vSendDifficultyChange("D1");
        }
        if (buttons.buttons[KEYCODE(2)]) {
			buttons.buttons[KEYCODE(2)] = 0;
            vSendDifficultyChange("D2");
        }
        if (buttons.buttons[KEYCODE(3)]) {
			buttons.buttons[KEYCODE(3)] = 0;
            vSendDifficultyChange("D3");
        }
		xSemaphoreGive(buttons.lock);
	}
}

void vCheckSendSpaceshipMothershipDiff(void)
{
    static TickType_t lastTimeSend = 0;

    if (xTaskGetTickCount() - lastTimeSend > pdMS_TO_TICKS(500)) {
        vSendSpaceshipMothershipDiff();
        lastTimeSend = xTaskGetTickCount();
    }
}

void vCheckSendBulletState(char *bullet_state)
{
    char bullet_state_tosend[2][12] = {"ATTACKING", "PASSIVE"};
    static char prev_bullet_state[12] = "PASSIVE";
    static int i = 0;

    if (strcmp(bullet_state, prev_bullet_state) != 0) {
        vSendBulletState(bullet_state_tosend[i]);
        strcpy(prev_bullet_state, bullet_state);
        i = !i;//swiches between both attacking and passive strings
    }
}

void vCheckBulletShoot(char *bullet_state)
{
    if (tumEventGetMouseLeft() && strcmp(bullet_state, "PASSIVE") == 0) {
        vShootBullet(my_spaceship.x + my_spaceship.width / 2,
                     my_spaceship.y, SPACESHIP_BULLET);
        tumSoundPlayUserSample("shoot.wav");
    }
}

void vGameLogic(void *pvParameters)
{
    char bullet_state[12] = "PASSIVE";

	while (1) {
        xGetButtonInput();

        vUpdateBulletPosition();
        if (my_player.n_players == 1)
            vUpdateMothershipPosition();
        else
            vUpdateMothershipPositionPVP();

        vCheckBulletColision();

        vSpaceshipBulletActive(bullet_state);
        vCheckBulletShoot(bullet_state);

        if (my_player.n_players == 2) {
            vCheckSendSpaceshipMothershipDiff();
            vCheckSendBulletState(bullet_state);
            vCheckMothershipDifficultyChange();
        }

        vCheckMonstersDead();
        vCheckPlayerDead();
        vCheckGameInput();

        vTaskDelay(pdMS_TO_TICKS(8));
	}
}

void vDrawGameText(void)
{
    vDrawScores();
    vDrawCredit();
    vDrawLevel();
    vDrawNumber(my_player.n_lives, 20, LOWER_TEXT_YLOCATION, 1);
}

void vDrawLives(void)
{
    if (my_player.n_lives >= 2)
        checkDraw(tumDrawLoadedImage(my_spaceship.image, 55,
                         LOWER_TEXT_YLOCATION + 5), __FUNCTION__);
    if (my_player.n_lives >= 3)
        checkDraw(tumDrawLoadedImage(my_spaceship.image,
                        55 + my_spaceship.width * 1.2, LOWER_TEXT_YLOCATION + 5),
                     __FUNCTION__);
}

void vDrawGameObjects(void)
{
    vDrawSpaceship();
    vDrawMonsters();
    vDrawBunkers();
    vDrawMothership();
    vDrawBullets();
    vDrawColisions();

    //draws line separating game and bottom of screen
    checkDraw(tumDrawFilledBox(0, GREEN_LINE_Y, SCREEN_WIDTH, 0, Green), __FUNCTION__);

    vDrawLives();
}

void vGameDrawer(void *pvParameters)
{
	while (1) {
		xSemaphoreTake(DrawSignal, portMAX_DELAY);
        tumEventFetchEvents(FETCH_EVENT_BLOCK |
				    FETCH_EVENT_NO_GL_CHECK);
		xSemaphoreTake(ScreenLock, portMAX_DELAY);
		checkDraw(tumDrawClear(BACKGROUND_COLOUR), __FUNCTION__);
		vDrawGameText();
        vDrawGameObjects();
		xSemaphoreGive(ScreenLock);
	}
}

void vShootMothershipBullet(void)
{
    if (my_player.n_players == 2)
        vShootBullet(my_mothership.x + my_mothership.width / 2,
                    my_mothership.y + my_mothership.height, MOTHERSHIP_BULLET);
}

int vChooseShooterColumn(void)
{
    return rand() % N_COLUMNS;
}

int vChooseShooterRow(int shooter_column)
{
    int i;

    for (i = N_ROWS - 1; i >= 0; i--) {
        if (my_monsters.monster[i][shooter_column].alive) {
            return i;
        }
    }

    return -1;
}

/**
 * @brief Chooses a random column of enemies
 * Of the choosen column scans through the monster bottom
 * to top and finds the first monster alive.
 * If no monster is alive returns repeats the process
 * until a column with a alive monster is found.
 * 
 * @param pvParameters 
 */
void vEnemyBulletShooter(void *pvParameters) {
    int shooter_column, shooter_row;

    while (1) {
        shooter_column = vChooseShooterColumn();
        
        shooter_row = vChooseShooterRow(shooter_column);
        if (shooter_row == -1)
            continue;

        vShootBullet(my_monsters.monster[shooter_row][shooter_column].x 
                     + my_monsters.monster[shooter_row][shooter_column].width / 2,
                     my_monsters.monster[shooter_row][shooter_column].y 
                     + my_monsters.monster[shooter_row][shooter_column].height,
                         MONSTER_BULLET);

        vShootMothershipBullet();

        vTaskDelay(pdMS_TO_TICKS(2500));
    }
}

void vMonsterMover(void *pvParameters)
{
    int i, j, direction = LEFT_TO_RIGHT;

    TickType_t monster_delay;

    while (1) {
        for (i = N_ROWS - 1 ; i >= 0; i--) {
            for (j = 0; j < N_COLUMNS; j++) {
                if (!vMoveMonster(i, j, direction)) {
                    continue;
                }
                xQueuePeek(MonsterDelayQueue, &monster_delay, portMAX_DELAY);
                vTaskDelay(pdMS_TO_TICKS(monster_delay));
            }
            vMonsterCallback();//Plays the monsters moving sound
        }
        vUpdateMonsterDirection(&direction);//only if any monster hits wall
    }
}

void vDrawPauseText(void)
{
    vDrawText("PAUSED", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 5, CENTERING);
    vDrawText("[A] AND [D] TO MOVE", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 4 + 25, CENTERING);
    vDrawText("LEFT CLICK TO SHOOT", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 4 + DEFAULT_FONT_SIZE * 1.5 + 25, CENTERING);

    if (my_player.n_players == 2) {
        vDrawText("MOTHERSHIP", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 + 20, CENTERING);
        vDrawText("DIFFICULTY:", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 + + DEFAULT_FONT_SIZE * 1.5 + 20, CENTERING);
        vDrawText("[1] EASY", SCREEN_WIDTH / 2, SCREEN_HEIGHT * 2 / 3, CENTERING);
        vDrawText("[2] MEDIUM", SCREEN_WIDTH / 2, SCREEN_HEIGHT * 2 / 3 + DEFAULT_FONT_SIZE * 1.5, CENTERING);
        vDrawText("[3] HARD", SCREEN_WIDTH / 2, SCREEN_HEIGHT * 2 / 3 + DEFAULT_FONT_SIZE * 3, CENTERING);
    }
}

void vPauseDrawer(void *pvParameters)
{
	while (1) {
		xSemaphoreTake(DrawSignal, portMAX_DELAY);
        tumEventFetchEvents(FETCH_EVENT_BLOCK |
				    FETCH_EVENT_NO_GL_CHECK);
        xGetButtonInput();
		xSemaphoreTake(ScreenLock, portMAX_DELAY);
		checkDraw(tumDrawClear(BACKGROUND_COLOUR), __FUNCTION__);
        vDrawPauseText();
		xSemaphoreGive(ScreenLock);
        vCheckPauseInput();
	}
}

void vInitImages(void)
{
    spaceship_image = tumDrawLoadImage("spaceship.png");
    monster_image[0] = tumDrawLoadImage("monster_spritesheet1.png");
    monster_image[1] = tumDrawLoadImage("monster_spritesheet2.png");
    monster_image[2] = tumDrawLoadImage("monster_spritesheet3.png");
    colision_image[0] = tumDrawLoadImage("colision1.png");
    colision_image[1] = tumDrawLoadImage("colision2.png");
    mothership_image = tumDrawLoadImage("mothership.png");
    bunker_image[0] = tumDrawLoadImage("bunker1.png");
    bunker_image[1] = tumDrawLoadImage("bunker2.png");
    bunker_image[2] = tumDrawLoadImage("bunker3.png");
    bunker_image[3] = tumDrawLoadImage("bunker4.png");
    bunker_image[4] = tumDrawLoadImage("bunker5.png");
    bunker_image[5] = tumDrawLoadImage("bunker6.png");
}

void vInitSpriteSheets(void)
{
    monster_spritesheet[0] = tumDrawLoadSpritesheet(monster_image[0], 2, 1);
    monster_spritesheet[1] = tumDrawLoadSpritesheet(monster_image[1], 2, 1);
    monster_spritesheet[2] = tumDrawLoadSpritesheet(monster_image[2], 2, 1);
}

void vInitSounds(void)
{
    tumSoundLoadUserSample("/home/rtos-sim/Desktop/SpaceInvadersESPL/resources/waveforms/fastinvader1.wav");
    tumSoundLoadUserSample("/home/rtos-sim/Desktop/SpaceInvadersESPL/resources/waveforms/invaderkilled.wav");
    tumSoundLoadUserSample("/home/rtos-sim/Desktop/SpaceInvadersESPL/resources/waveforms/shoot.wav");
    tumSoundLoadUserSample("/home/rtos-sim/Desktop/SpaceInvadersESPL/resources/waveforms/ufo_highpitch.wav");
    tumSoundLoadUserSample("/home/rtos-sim/Desktop/SpaceInvadersESPL/resources/waveforms/explosion.wav");
}

void vSaveHighScore(void)
{
    FILE *fp = NULL;

    fp = fopen("highscore.txt", "w");
    fprintf(fp, "Last session's highscore was %d.\n", my_player.highscore);
    fclose(fp);
}

/**
 * @brief Data can still be received even if player selects 1 player mode or is in menu
 * If thats the case, then doesn't do anything with the received data
 * 
 * @return Return 1 if can receive data, 0 otherwise
 */
int vCheckCanReceiveData(void)
{
    int current_state;

    if (xQueuePeek(CurrentStateQueue, &current_state, 0) == pdTRUE) {
        if (current_state == GAME && my_player.n_players == 2) {
            return 1;
        }
    }

    return 0;
}

#define PRINT_TASK_ERROR(task) PRINT_ERROR("Failed to print task ##task");

int main(int argc, char *argv[])
{
	char *bin_folder_path = tumUtilGetBinFolderPath(argv[0]);

	prints("Initializing: ");

	//  Note PRINT_ERROR is not thread safe and is only used before the
	//  scheduler is started. There are thread safe print functions in
	//  TUM_Print.h, `prints` and `fprints` that work exactly the same as
	//  `printf` and `fprintf`. So you can read the documentation on these
	//  functions to understand the functionality.

    // INITIALIZING EVERYTHING:
	if (tumDrawInit(bin_folder_path)) {
		PRINT_ERROR("Failed to intialize drawing");
		goto err_init_drawing;
	} else {
		prints("drawing");
	}

	if (tumEventInit()) {
		PRINT_ERROR("Failed to initialize events");
		goto err_init_events;
	} else {
		prints(", events");
	}

	if (tumSoundInit(bin_folder_path)) {
		PRINT_ERROR("Failed to initialize audio");
		goto err_init_audio;
	} else {
		prints(", and audio\n");
	}

	if (safePrintInit()) {
		PRINT_ERROR("Failed to init safe print");
		goto err_init_safe_print;
	}

	//Semaphores/Mutexes
	buttons.lock = xSemaphoreCreateMutex(); // Locking mechanism
	if (!buttons.lock) {
		PRINT_ERROR("Failed to create buttons lock");
		goto err_buttons_lock;
	}

	DrawSignal = xSemaphoreCreateBinary(); // Screen buffer locking
	if (!DrawSignal) {
		PRINT_ERROR("Failed to create draw signal");
		goto err_draw_signal;
	}
	ScreenLock = xSemaphoreCreateMutex();
	if (!ScreenLock) {
		PRINT_ERROR("Failed to create screen lock");
		goto err_screen_lock;
	}

	//Queues
	StateChangeQueue =
		xQueueCreate(UNITARY_QUEUE_LENGHT, sizeof(unsigned char));
	if (!StateChangeQueue) {
		PRINT_ERROR("Could not open state change queue");
		goto err_state_change_queue;
	}

	CurrentStateQueue =
		xQueueCreate(UNITARY_QUEUE_LENGHT, sizeof(unsigned char));
	if (!CurrentStateQueue) {
		PRINT_ERROR("Could not open current state queue");
		goto err_current_state_queue;
	}

    BulletQueue =
		xQueueCreate(MAX_OBJECTS, sizeof(bullet_t));
	if (!BulletQueue) {
		PRINT_ERROR("Could not open bullet queue");
		goto err_bullet_queue;
	}

    ColisionQueue =
		xQueueCreate(MAX_OBJECTS, sizeof(colision_t));
	if (!ColisionQueue) {
		PRINT_ERROR("Could not open colision queue");
		goto err_colision_queue;
	}

    TimerStartingQueue =
		xQueueCreate(UNITARY_QUEUE_LENGHT, sizeof(TickType_t));
	if (!TimerStartingQueue) {
		PRINT_ERROR("Could not open timer value queue");
		goto err_timer_starting_queue;
	}

    MonsterDelayQueue =
		xQueueCreate(UNITARY_QUEUE_LENGHT, sizeof(TickType_t));
	if (!MonsterDelayQueue) {
		PRINT_ERROR("Could not open monster delay queue");
		goto err_monster_delay_queue;
	}

    LevelQueue =
		xQueueCreate(UNITARY_QUEUE_LENGHT, sizeof(int));
	if (!LevelQueue) {
		PRINT_ERROR("Could not open level queue");
		goto err_level_queue;
	}

	//Timers
    xMothershipTimer = xTimerCreate("Mothership Timer", pdMS_TO_TICKS(ORIGINAL_TIMER),
                                     pdTRUE, (void *) 0, vMothershipTimerCallback);
    if (xMothershipTimer == NULL) {
        PRINT_ERROR("Could not open mothership timer");
        goto err_mothership_timer;
    }

	//Infrastructure Tasks
	if (xTaskCreate(basicSequentialStateMachine, "StateMachine",
			STACK_SIZE, NULL,
			configMAX_PRIORITIES - 1, &StateMachine) != pdPASS) {
		PRINT_TASK_ERROR("StateMachine");
		goto err_statemachine;
	}
	if (xTaskCreate(vSwapBuffers, "BufferSwapTask",
			STACK_SIZE, NULL, configMAX_PRIORITIES,
			&BufferSwap) != pdPASS) {
		PRINT_TASK_ERROR("BufferSwapTask");
		goto err_bufferswap;
	}

	if (xTaskCreate(vGameLogic, "GameLogic", STACK_SIZE,
			NULL, mainGENERIC_PRIORITY + 2, &GameLogic) != pdPASS) {
		PRINT_TASK_ERROR("GameLogic");
		goto err_game_logic;
	}

    if (xTaskCreate(vEnemyBulletShooter, "EnemyBulletShooter", STACK_SIZE,
			NULL, mainGENERIC_PRIORITY + 3, &EnemyBulletShooter) != pdPASS) {
		PRINT_TASK_ERROR("EnemyBulletShooter");
		goto err_monster_bullet_shooter;
	}

    if (xTaskCreate(vMonsterMover, "MonsterMover", STACK_SIZE,
			NULL, mainGENERIC_PRIORITY, &MonsterMover) != pdPASS) {
		PRINT_TASK_ERROR("MonsterMover");
		goto err_monster_mover;
	}

    if (xTaskCreate(vCheckInputSetScore, "CheckInputSetScore", STACK_SIZE,
			NULL, mainGENERIC_PRIORITY,
			&CheckInputSetScore) != pdPASS) {
		PRINT_TASK_ERROR("CheckInputSetScore");
		goto err_CheckInputSetScore;
	}

	//State Drawer Tasks
	if (xTaskCreate(vMenuDrawer, "MenuDrawer", STACK_SIZE,
			NULL, mainGENERIC_PRIORITY + 1,
			&MenuDrawer) != pdPASS) {
		PRINT_TASK_ERROR("MenuDrawer");
		goto err_MenuDrawer;
	}

	GameDrawer = xTaskCreateStatic(vGameDrawer, "GameDrawer", STACK_SIZE, NULL,
				       mainGENERIC_PRIORITY + 1, xStack,
				       &GameDrawerBuffer);
	if (GameDrawer == NULL) {
		PRINT_TASK_ERROR("GameDrawer");
		goto err_GameDrawer;
	}

    if (xTaskCreate(vPauseDrawer, "PauseDrawer", STACK_SIZE,
			NULL, mainGENERIC_PRIORITY + 1,
			&PauseDrawer) != pdPASS) {
		PRINT_TASK_ERROR("PauseDrawer");
		goto err_PauseDrawer;
	}

    srand(time(NULL));

    vInitImages();
    vInitSpriteSheets();
    vInitSounds();

    vInitPlayer();
    vInitSpaceship(spaceship_image);
    vInitMonsters(monster_image, monster_spritesheet);
    vInitMonsterDelay();
    vInitMothership(mothership_image);
    vInitBunkers(bunker_image);
    vInitSavedValues();
    vInitPVP();

    atexit(vSaveHighScore);
    atexit(aIODeinit);
    atexit(vObjectSemaphoreDelete);

    printf("Welcome to Space Invaders Remastered HD!\n");

	vSuspendAllTasks();

	vTaskStartScheduler();

	return EXIT_SUCCESS;
	vTaskDelete(PauseDrawer);
err_PauseDrawer:
	vTaskDelete(GameDrawer);
err_GameDrawer:
	vTaskDelete(MenuDrawer);
err_MenuDrawer:
    vTaskDelete(CheckInputSetScore);
err_CheckInputSetScore:
    vTaskDelete(MonsterMover);
err_monster_mover:
    vTaskDelete(EnemyBulletShooter);
err_monster_bullet_shooter:
	vTaskDelete(GameLogic);
err_game_logic:
	vTaskDelete(BufferSwap);
err_bufferswap:
	vTaskDelete(StateMachine);
err_statemachine:
    xTimerDelete(xMothershipTimer, portMAX_DELAY);
err_mothership_timer:
    vQueueDelete(LevelQueue);
err_level_queue:
    vQueueDelete(MonsterDelayQueue);
err_monster_delay_queue:
    vQueueDelete(TimerStartingQueue);
err_timer_starting_queue:
    vQueueDelete(ColisionQueue);
err_colision_queue:
    vQueueDelete(BulletQueue);
err_bullet_queue:
	vQueueDelete(CurrentStateQueue);
err_current_state_queue:
	vQueueDelete(StateChangeQueue);
err_state_change_queue:
	vSemaphoreDelete(ScreenLock);
err_screen_lock:
	vSemaphoreDelete(DrawSignal);
err_draw_signal:
	vSemaphoreDelete(buttons.lock);
err_buttons_lock:
	safePrintExit();
err_init_safe_print:
	tumSoundExit();
err_init_audio:
	tumEventExit();
err_init_events:
	tumDrawExit();
err_init_drawing:
	return EXIT_FAILURE;
}

// cppcheck-suppress unusedFunction
__attribute__((unused)) void vMainQueueSendPassed(void)
{
	/* This is just an example implementation of the "queue send" trace hook. */
}

// cppcheck-suppress unusedFunction
__attribute__((unused)) void vApplicationIdleHook(void)
{
#ifdef __GCC_POSIX__
	struct timespec xTimeToSleep, xTimeSlept;
	/* Makes the process more agreeable when using the Posix simulator. */
	xTimeToSleep.tv_sec = 1;
	xTimeToSleep.tv_nsec = 0;
	nanosleep(&xTimeToSleep, &xTimeSlept);
#endif
}

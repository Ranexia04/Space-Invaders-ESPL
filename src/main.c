#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

#include <SDL2/SDL_scancode.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"
#include "timers.h"
#include "string.h"

#include "TUM_Ball.h"
#include "TUM_Draw.h"
#include "TUM_Font.h"
#include "TUM_Event.h"
#include "TUM_Sound.h"
#include "TUM_Utils.h"
#include "TUM_FreeRTOS_Utils.h"
#include "TUM_Print.h"

//#include "AsyncIO.h"

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

#define NOT_CENTERING 0
#define CENTERING 1

#define KEYCODE(CHAR) SDL_SCANCODE_##CHAR
#define BACKGROUND_COLOUR Black
#define TEXT_COLOUR White
#define OBJECT_COLOUR Green
#define PI 3.142857
#define FREQ 1
#define RADIUS 40
#define GREEN_LINE_Y SCREEN_HEIGHT - 38
#define TOP_LINE_Y 75
#define SPACESHIP_Y SCREEN_HEIGHT - 75
#define MOTHERGUNSHIP_Y 83
#define BULLET_HEIGHT 5
#define N_ROWS 5
#define N_COLUMNS 11
#define SPACESHIP_BULLET 0
#define MONSTER_BULLET 1
#define MOTHERGUNSHIP_BULLET 2
#define N_BUNKERS 4
#define ORIGINAL_TIMER 10000
#define ORIGINAL_MONSTER_DELAY 65
#define MAX_OBJECTS 10
#define MONSTER_SPACING_V 34
#define MONSTER_SPACING_H 39

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
static TaskHandle_t MonsterBulletShooter = NULL;
static TaskHandle_t MonsterMover = NULL;
static TaskHandle_t PauseDrawer = NULL;

static TimerHandle_t xMothergunshipTimer;

static StaticTask_t GameDrawerBuffer;
static StackType_t xStack[STACK_SIZE];

static QueueHandle_t StateChangeQueue = NULL;
static QueueHandle_t CurrentStateQueue = NULL;
static QueueHandle_t BulletQueue = NULL;
static QueueHandle_t ColisionQueue = NULL;
static QueueHandle_t TimerStartingQueue = NULL;
static QueueHandle_t MonsterDelayQueue = NULL;

static SemaphoreHandle_t DrawSignal = NULL;
static SemaphoreHandle_t ScreenLock = NULL;

static image_handle_t spaceship_image = NULL;
static image_handle_t monster_image[3] = {NULL};
static image_handle_t colision_image[2] = {NULL};
static image_handle_t mothergunship_image = NULL;
static image_handle_t bunker_image[6] = {NULL};

static spritesheet_handle_t monster_spritesheet[3] = {NULL};

typedef struct buttons_buffer {
	unsigned char buttons[SDL_NUM_SCANCODES];
	SemaphoreHandle_t lock;
} buttons_buffer_t;

static buttons_buffer_t buttons = { 0 };

typedef struct spaceship {
    image_handle_t image;

    int x; /**< X pixel coord of ball on screen */
    int y; /**< Y pixel coord of ball on screen */

    signed short width;
    signed short height;

    float f_x; /**< Absolute X location of ball */
    float f_y; /**< Absolute Y location of ball */

    callback_t callback; /**< Collision callback */
    void *args; /**< Collision callback args */
    SemaphoreHandle_t lock;
} spaceship_t;

static spaceship_t my_spaceship = { 0 };

typedef struct bullet {
    int x; /**< X pixel coord of spaceship on screen */
    int y; /**< Y pixel coord of spaceship on screen */

    signed short width;
    signed short height;

    unsigned int colour;

    int type;

    callback_t callback; /**< bullet callback */
    void *args; /**< bullet callback args */
} bullet_t;

typedef struct colision {
    image_handle_t image;

    int x; /**< X pixel coord of colision on screen */
    int y; /**< Y pixel coord of colision on screen */

    signed short width;
    signed short height;

    int frame_number;

    callback_t callback; /**< Collision callback */
    void *args; /**< Collision callback args */
} colision_t;

typedef struct monster {
    spritesheet_handle_t spritesheet;
    
    int todraw;

    int x; /**< X pixel coord of monster on screen */
    int y; /**< Y pixel coord of monster on screen */

    signed short width;
    signed short height;

    int type;
    int alive;

    callback_t callback; /**< monster callback */
    void *args; /**< monster callback args */
} monster_t;

typedef struct monster_grid {
    monster_t monster[N_ROWS][N_COLUMNS];
    SemaphoreHandle_t lock;
} monster_grid_t;

static monster_grid_t my_monsters = { 0 };

typedef struct player {
    int score1;
    int highscore;
    int score2;
    int n_lives;
    int credits;

    SemaphoreHandle_t lock;
} player_t;

static player_t my_player = { 0 };

typedef struct mothergunship {
    image_handle_t image;
    int x; /**< X pixel coord of mothergunship on screen */
    int y; /**< Y pixel coord of mothergunship on screen */

    signed short width;
    signed short height;

    int alive;
    int direction;

    SemaphoreHandle_t lock;
} mothergunship_t;

static mothergunship_t my_mothergunship = { 0 };

typedef struct bunker_component {
    image_handle_t image;
    int x; /**< X pixel coord of mothergunship on screen */
    int y; /**< Y pixel coord of mothergunship on screen */

    signed short width;
    signed short height;

    int damage;
} bunker_component_t;

typedef struct bunker {
    bunker_component_t component[2][3];
} bunker_t;

typedef struct bunker_grid {
    bunker_t bunker[N_BUNKERS];
    SemaphoreHandle_t lock;
} bunker_grid_t;

static bunker_grid_t my_bunkers = {0};

struct saved_values {
    int n_lives;
    int score;
    int offset;
    int credits;
    SemaphoreHandle_t lock;
};

struct saved_values saved = {0};

void vResetSpaceship(void)
{
    xSemaphoreTake(my_spaceship.lock, portMAX_DELAY);
    my_spaceship.x = SCREEN_WIDTH / 2 - my_spaceship.width / 2;
    xSemaphoreGive(my_spaceship.lock);
}

void vInitSpaceship(void)
{
    my_spaceship.image = spaceship_image;
    my_spaceship.width = tumDrawGetLoadedImageWidth(my_spaceship.image);
    my_spaceship.height = tumDrawGetLoadedImageHeight(my_spaceship.image);
    my_spaceship.x = SCREEN_WIDTH / 2 - my_spaceship.width / 2;
    my_spaceship.y = SPACESHIP_Y;
    my_spaceship.lock = xSemaphoreCreateMutex();
}

void vResetMonsters(void)
{
    int i, j;

    xSemaphoreTake(my_monsters.lock, portMAX_DELAY);
    for (i = 0; i < N_ROWS; i++) {
        for (j = 0; j < N_COLUMNS; j++) {
            my_monsters.monster[i][j].x = 15 + MONSTER_SPACING_H * j;
            my_monsters.monster[i][j].y = SCREEN_HEIGHT / 4 + MONSTER_SPACING_V * i;
            my_monsters.monster[i][j].alive = 1;
            my_monsters.monster[i][j].todraw = 0;
        }
    }
    xSemaphoreGive(my_monsters.lock);
}

void vInitMonsters(void)
{
    int i, j;

    for (i = 0; i < N_ROWS; i++) {
        for (j = 0; j < N_COLUMNS; j++) {
            if (i == 0) {
                my_monsters.monster[i][j].type = 0;
                my_monsters.monster[i][j].spritesheet = monster_spritesheet[0];
                my_monsters.monster[i][j].width = tumDrawGetLoadedImageWidth(monster_image[0]) / 2;
                my_monsters.monster[i][j].height = tumDrawGetLoadedImageHeight(monster_image[0]);
            }
            if (i == 1 || i == 2) {
                my_monsters.monster[i][j].type = 1;
                my_monsters.monster[i][j].spritesheet = monster_spritesheet[1];
                my_monsters.monster[i][j].width = tumDrawGetLoadedImageWidth(monster_image[1]) / 2;
                my_monsters.monster[i][j].height = tumDrawGetLoadedImageHeight(monster_image[1]);
            }
            if (i == 3 || i == 4) {
                my_monsters.monster[i][j].type = 2;
                my_monsters.monster[i][j].spritesheet = monster_spritesheet[2];
                my_monsters.monster[i][j].width = tumDrawGetLoadedImageWidth(monster_image[2]) / 2;
                my_monsters.monster[i][j].height = tumDrawGetLoadedImageHeight(monster_image[2]);
            }
            my_monsters.monster[i][j].x = 15 + MONSTER_SPACING_H * j;
            my_monsters.monster[i][j].y = SCREEN_HEIGHT / 4 + MONSTER_SPACING_V * i;
            my_monsters.monster[i][j].alive = 1;
            my_monsters.monster[i][j].todraw = 0;
        }
    }
    my_monsters.lock = xSemaphoreCreateMutex();
}

void vResetPlayer(void)
{
    xSemaphoreTake(my_player.lock, portMAX_DELAY);
    if (my_player.score1 > my_player.highscore) {
        my_player.highscore = my_player.score1;
    }
    my_player.score1 = saved.score;
    my_player.score2 = 0;
    my_player.n_lives = saved.n_lives;
    my_player.credits = saved.credits;
    xSemaphoreGive(my_player.lock);
}

void vInitPlayer(void)
{
    my_player.score1 = 0;
    my_player.highscore = 0;
    my_player.score2 = 0;
    my_player.n_lives = 3;
    my_player.credits = 0;
    my_player.lock = xSemaphoreCreateMutex();
}

void vResetMonsterDelay(void)
{
    TickType_t monster_delay;

    monster_delay = ORIGINAL_MONSTER_DELAY + saved.offset;
    xQueueOverwrite(MonsterDelayQueue, &monster_delay);
}

void vUpdateMonsterDelay(void)
{
    TickType_t monster_delay;

    xQueuePeek(MonsterDelayQueue, &monster_delay, portMAX_DELAY);
    monster_delay--;
    xQueueOverwrite(MonsterDelayQueue, &monster_delay);
}

void vInitMonsterDelay(void)
{
    TickType_t monster_delay = ORIGINAL_MONSTER_DELAY;

    xQueueOverwrite(MonsterDelayQueue, &monster_delay);
}

void vUpdateSavedValues(void)
{
    int monster_delay;

    xSemaphoreTake(saved.lock, portMAX_DELAY);
    saved.n_lives = my_player.n_lives;
    saved.score = my_player.score1;
    xQueuePeek(MonsterDelayQueue, &monster_delay, portMAX_DELAY);
    saved.offset = monster_delay - ORIGINAL_MONSTER_DELAY;
    saved.credits = my_player.credits - 1;
    xSemaphoreGive(saved.lock);
}

void vInitSavedValues(void)
{
    saved.n_lives = 3;
    saved.score = 0;
    saved.offset = 0;
    saved.credits = 0;
    saved.lock = xSemaphoreCreateMutex();
}

#define LEFT_TO_RIGHT 0
#define RIGHT_TO_LEFT 1

void vResetMothergunship(void)
{
    TickType_t timer_starting = xTaskGetTickCount();

    xSemaphoreTake(my_mothergunship.lock, portMAX_DELAY);
    my_mothergunship.direction = !my_mothergunship.direction;
    if (my_mothergunship.direction == LEFT_TO_RIGHT)
        my_mothergunship.x = -1 * my_mothergunship.width;
    if (my_mothergunship.direction == RIGHT_TO_LEFT)
        my_mothergunship.x = SCREEN_WIDTH;
    my_mothergunship.alive = 1;
    xSemaphoreGive(my_mothergunship.lock);
    xTimerChangePeriod(xMothergunshipTimer, ORIGINAL_TIMER, portMAX_DELAY);

    xQueueOverwrite(TimerStartingQueue, &timer_starting);
}

void vMothergunshipTimerCallback(TimerHandle_t xMothergunshipTimer)
{
    vResetMothergunship();
}

void vKillMothergunship(void)
{
    xSemaphoreTake(my_mothergunship.lock, portMAX_DELAY);
    my_mothergunship.alive = 0;
    xSemaphoreGive(my_mothergunship.lock);
}

void vInitMothergunship(void)
{
    TickType_t timer_starting = xTaskGetTickCount();

    my_mothergunship.image = mothergunship_image;
    my_mothergunship.x = 0;
    my_mothergunship.y = MOTHERGUNSHIP_Y;
    my_mothergunship.width = tumDrawGetLoadedImageWidth(my_mothergunship.image);
    my_mothergunship.height = tumDrawGetLoadedImageHeight(my_mothergunship.image);
    my_mothergunship.alive = 0;
    my_mothergunship.direction = RIGHT_TO_LEFT;
    my_mothergunship.lock = xSemaphoreCreateMutex();

    xQueueOverwrite(TimerStartingQueue, &timer_starting);
}

void vResetBunkers(void)
{
    int i, j, k;

    xSemaphoreTake(my_bunkers.lock, portMAX_DELAY);
    for (k = 0; k < N_BUNKERS; k++) {
        for (i = 0; i < 2; i++) {
            for (j = 0; j < 3; j++) {
                my_bunkers.bunker[k].component[i][j].damage = 0;
            }
        }
    }
    xSemaphoreGive(my_bunkers.lock);
}

void vInitBunkers(void)
{
    int i, j, k;

    for (k = 0; k < N_BUNKERS; k++) {
        for (i = 0; i < 2; i++) {
            for (j = 0; j < 3; j++) {
                if (i == 0 && j == 0) {
                    my_bunkers.bunker[k].component[i][j].image = bunker_image[0];
                }
                if (i == 0 && j == 1) {
                    my_bunkers.bunker[k].component[i][j].image = bunker_image[1];
                }
                if (i == 0 && j == 2) {
                    my_bunkers.bunker[k].component[i][j].image = bunker_image[2];
                }
                if (i == 1 && j == 0) {
                    my_bunkers.bunker[k].component[i][j].image = bunker_image[3];
                }
                if (i == 1 && j == 1) {
                    my_bunkers.bunker[k].component[i][j].image = bunker_image[4];
                }
                if (i == 1 && j == 2) {
                    my_bunkers.bunker[k].component[i][j].image = bunker_image[5];
                }
                my_bunkers.bunker[k].component[i][j].width = tumDrawGetLoadedImageWidth(my_bunkers.bunker[k].component[i][j].image);
                my_bunkers.bunker[k].component[i][j].height = tumDrawGetLoadedImageHeight(my_bunkers.bunker[k].component[i][j].image);
                my_bunkers.bunker[k].component[i][j].x = SCREEN_WIDTH * k / N_BUNKERS + j * my_bunkers.bunker[k].component[i][j].width + 30;
                my_bunkers.bunker[k].component[i][j].y = 375 + my_bunkers.bunker[k].component[i][j].height * i;
                my_bunkers.bunker[k].component[i][j].damage = 0;
            }
        }
    }

    my_bunkers.lock = xSemaphoreCreateMutex();
}

void vResetQueues(void)
{
    bullet_t bullet;
    colision_t colision;

    while (uxQueueMessagesWaiting(BulletQueue)) {
        xQueueReceive(BulletQueue, &bullet, portMAX_DELAY);
    }

    while (uxQueueMessagesWaiting(ColisionQueue)) {
        xQueueReceive(ColisionQueue, &colision, portMAX_DELAY);
    }
}

void vInsertCoin(void)
{
    xSemaphoreTake(my_player.lock, portMAX_DELAY);
    my_player.credits++;
    xSemaphoreGive(my_player.lock);
    prints("Coin Inserted.\n");
}

void vUseCoin(void)
{
    xSemaphoreTake(my_player.lock, portMAX_DELAY);
    my_player.credits--;
    xSemaphoreGive(my_player.lock);
}

void vCheckCoinInput(void)
{
    static int debounce_flag = 0;

    if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
		if (buttons.buttons[KEYCODE(C)]) {
			if (!debounce_flag) {
                debounce_flag = 1;
                vInsertCoin();
            }
		} else {
            debounce_flag = 0;
        }
		xSemaphoreGive(buttons.lock);
	}
}

void vResetGameBoard(void)
{
    vResetQueues();
    vResetMonsters();
    vResetMonsterDelay();
    vResetSpaceship();
    vResetBunkers();
    vKillMothergunship();
    xTimerReset(xMothergunshipTimer, portMAX_DELAY);
}

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

void xGetButtonInput(void)
{
	if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
		xQueueReceive(buttonInputQueue, &buttons.buttons, 0);
		xSemaphoreGive(buttons.lock);
	}
}

static int vCheckPauseInput(void)
{
    unsigned char current_state;

	if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
		if (buttons.buttons[KEYCODE(P)]) {
			buttons.buttons[KEYCODE(P)] = 0;
			if (StateChangeQueue) {
				xSemaphoreGive(buttons.lock);
                if (xQueuePeek(CurrentStateQueue, &current_state, 0) ==
			    pdTRUE) {
                    if (current_state == GAME) {
                        xQueueSend(StateChangeQueue, &next_state_signal, 0);
                    } else if (current_state == PAUSE) {
                        xQueueSend(StateChangeQueue, &prev_state_signal, 0);
                    }
				}
				return 0;
			}
			xSemaphoreGive(buttons.lock);
			return -1;
		}
		xSemaphoreGive(buttons.lock);
	}

	return 0;
}

static int vCheckStateInput(void)
{
    unsigned char current_state;

	if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
		if (buttons.buttons[KEYCODE(M)]) {
			buttons.buttons[KEYCODE(M)] = 0;
			if (StateChangeQueue) {
				xSemaphoreGive(buttons.lock);
                if (xQueuePeek(CurrentStateQueue, &current_state, 0) ==
			    pdTRUE) {
                    if (current_state == MENU && my_player.credits > 0) {
                        vUseCoin();
                        xQueueSend(StateChangeQueue, &next_state_signal, 0);
                    } else if (current_state == GAME) {
                        xQueueSend(StateChangeQueue, &prev_state_signal, 0);
                    }
				}
				return 0;
			}
			xSemaphoreGive(buttons.lock);
			return -1;
		}
		xSemaphoreGive(buttons.lock);
	}

	return 0;
}

#define UPPER_TEXT_YLOCATION 10
#define LOWER_TEXT_YLOCATION SCREEN_HEIGHT - DEFAULT_FONT_SIZE - 20

void vAddSpaces(char *str)
{
    int index = 0;
    char buffer[30], buffer2[30];

    strcpy(buffer, str);
    strcpy(str, "");

    while ( buffer[index] != '\0') {
        sprintf(buffer2, "%c ", buffer[index]);
        strcat(str, buffer2);
        index++;
    }
        
}

void vGetNumberString(char *str, int number, int n_digits)
{
    char num_str[30];
	int index = 0, i;
    
    sprintf(num_str, "%d", number);
    while ( num_str[index] != '\0' ) {
        index++;
    }
    index--;//index is now the index of the last digit of the number we want to draw
    
    i = n_digits;
    str[i] = '\0';
    i--;

    while ( index >= 0 ) {
        str[i] = num_str[index];
        index--;
        i--;
    }
    while ( i >= 0 ) {
        str[i] = '0';
        i--;
    }
}

void vDrawText(char *buffer, int x, int y, int centering)
{
    char str[30] = {'\0'};
	int text_width;

    strcpy(str, buffer);
    vAddSpaces(str);
	tumGetTextSize(str, &text_width, NULL);
    if ( centering == CENTERING ) {
        x = x - text_width / 2;
        y = y - DEFAULT_FONT_SIZE / 2;
    }
	checkDraw(tumDrawText(str, x, y, TEXT_COLOUR), __FUNCTION__);
}

void vGetMaxNumber(int *max_number, int n_digits)
{
    for ( int i = 0; i < n_digits; i++) {
        (*max_number) = (*max_number) + 9 * pow(10, i);
    }
}

void vDrawNumber(int number, int x, int y, int n_digits)
{
    char str[30];
    int max_number = 0;

    vGetMaxNumber(&max_number, n_digits);

    if ( number > max_number ) {
        sprintf(str, "%d", max_number);
    } else {
        vGetNumberString(str, number, n_digits);
    }
    
    vAddSpaces(str);
	vDrawText(str, x, y, NOT_CENTERING);
}

void vDrawScores(void)
{
    vDrawText("SCORE<1>", 10, UPPER_TEXT_YLOCATION, NOT_CENTERING);
    vDrawNumber(my_player.score1, 45, UPPER_TEXT_YLOCATION + DEFAULT_FONT_SIZE * 1.3, 4);
    vDrawText("HI-SCORE", SCREEN_WIDTH / 3 + 10, UPPER_TEXT_YLOCATION, NOT_CENTERING);
    vDrawNumber(my_player.highscore, SCREEN_WIDTH / 3 + 25, UPPER_TEXT_YLOCATION + DEFAULT_FONT_SIZE * 1.3, 4);
    vDrawText("SCORE<2>", SCREEN_WIDTH * 2 / 3 + 10, UPPER_TEXT_YLOCATION, NOT_CENTERING);
}

void vDrawCredit(void)
{
    vDrawText("CREDIT", SCREEN_WIDTH * 2 / 3 - 20, LOWER_TEXT_YLOCATION, NOT_CENTERING);
    vDrawNumber(my_player.credits, SCREEN_WIDTH - 55, LOWER_TEXT_YLOCATION, 2);
}

void vDrawGameText(void)
{
    vDrawScores();
    vDrawCredit();
    vDrawNumber(my_player.n_lives, 20, LOWER_TEXT_YLOCATION, 1);

}

void vTaskSuspender()
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
    if (MonsterBulletShooter) {
		vTaskSuspend(MonsterBulletShooter);
    }
    if (MonsterMover) {
		vTaskSuspend(MonsterMover);
    }
    if (PauseDrawer) {
		vTaskSuspend(PauseDrawer);
    }
}

void basicSequentialStateMachine(void *pvParameters)
{
	unsigned char current_state = STARTING_STATE; // Default state
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
			vTaskSuspender();
			switch (current_state) {
			case MENU:
                if (prev_state == GAME) {
                    vResetGameBoard();
                    vResetPlayer();
                    xTimerStop(xMothergunshipTimer, portMAX_DELAY);
                    prints("Match exited.\n");
                }
				if (MenuDrawer) {
					vTaskResume(MenuDrawer);
				}
				break;
			case GAME:
                if (prev_state == MENU || prev_state == current_state) {
                    xTimerReset(xMothergunshipTimer, portMAX_DELAY);
                    prints("Match started! Good luck and Have fun!\n");
                }
                if (prev_state == PAUSE) {
                    xTimerChangePeriod(xMothergunshipTimer, ORIGINAL_TIMER - timer_elapsed, portMAX_DELAY);
                    prints("Game unpaused.\n");
                }
				if (GameDrawer) {
					vTaskResume(GameDrawer);
				}
				if (GameLogic) {
					vTaskResume(GameLogic);
                }
                if (MonsterBulletShooter) {
					vTaskResume(MonsterBulletShooter);
                }
                if (MonsterMover) {
					vTaskResume(MonsterMover);
                }
				break;
            case PAUSE:
                if (prev_state == GAME) {
                    prints("Game paused.\n");
                    xTimerStop(xMothergunshipTimer, portMAX_DELAY);
                    timer_starting = xQueuePeek(TimerStartingQueue, &timer_starting, portMAX_DELAY);
                    timer_elapsed = xTaskGetTickCount() - timer_starting;
                }
                if (PauseDrawer) {
					vTaskResume(PauseDrawer);
                }
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

#define CHANGE_IN_POSITION 1

void vMoveSpaceshipLeft(void)
{
    if (xSemaphoreTake(my_spaceship.lock, 0) == pdTRUE) {
        my_spaceship.x = my_spaceship.x - CHANGE_IN_POSITION;
        if (my_spaceship.x < 0) {
            my_spaceship.x = 0;
        }
        xSemaphoreGive(my_spaceship.lock);
    }
}

void vMoveSpaceshipRight(void)
{
    if (xSemaphoreTake(my_spaceship.lock, 0) == pdTRUE) {
        my_spaceship.x = my_spaceship.x + CHANGE_IN_POSITION;
        if (my_spaceship.x + my_spaceship.width > SCREEN_WIDTH) {
            my_spaceship.x = SCREEN_WIDTH - my_spaceship.width;
        }
        xSemaphoreGive(my_spaceship.lock);
    }
}

int vCheckButtonInput(int key)
{
	if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
		if (buttons.buttons[key]) {
			xSemaphoreGive(buttons.lock);
			switch (key) {
				case KEYCODE(A):
                    vMoveSpaceshipLeft();
					break;
				case KEYCODE(D):
                    vMoveSpaceshipRight();
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

void vCheckKeyboardInput(void)
{
	vCheckStateInput();
	vCheckButtonInput(KEYCODE(A));
	vCheckButtonInput(KEYCODE(D));
}

void vDrawMenuText(void)
{
    vDrawScores();
    if (my_player.credits) {
        vDrawText("PLAY", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 4, CENTERING);
        vDrawText("SPACE INVADERS", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 3, CENTERING);
        vDrawText("SCORE ADVANCE TABLE", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2, CENTERING);
        checkDraw(tumDrawLoadedImage(mothergunship_image, SCREEN_WIDTH / 5 - 5, SCREEN_HEIGHT / 2 + DEFAULT_FONT_SIZE * 1.5 - 10), __FUNCTION__);
        vDrawText("=? MYSTERY", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 + DEFAULT_FONT_SIZE * 1.5, CENTERING);
        checkDraw(tumDrawSprite(monster_spritesheet[0], 0, 0, SCREEN_WIDTH / 4 + 3, SCREEN_HEIGHT / 2 + DEFAULT_FONT_SIZE * 3 - 5), __FUNCTION__);
        vDrawText("=30 POINTS", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 + DEFAULT_FONT_SIZE * 3, CENTERING);
        checkDraw(tumDrawSprite(monster_spritesheet[1], 0, 0, SCREEN_WIDTH / 4, SCREEN_HEIGHT / 2 + DEFAULT_FONT_SIZE * 4.5 - 5), __FUNCTION__);
        vDrawText("=20 POINTS", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 + DEFAULT_FONT_SIZE * 4.5, CENTERING);
        checkDraw(tumDrawSprite(monster_spritesheet[2], 0, 0, SCREEN_WIDTH / 4, SCREEN_HEIGHT / 2 + DEFAULT_FONT_SIZE * 6 - 5), __FUNCTION__);
        vDrawText("=10 POINTS", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 + DEFAULT_FONT_SIZE * 6, CENTERING);
    } else {
        vDrawText("INSERT  COIN", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 3, CENTERING);
        vDrawText("<1 OR 2 PLAYERS>", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2, CENTERING);
        vDrawText("1 PLAYER", SCREEN_WIDTH / 2, SCREEN_HEIGHT * 2 / 3, CENTERING);
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
        my_player.n_lives = 3;
        prints("You now have standard ammount of lives!\n");
        xSemaphoreGive(my_player.lock);
        return;
    }
    xSemaphoreGive(my_player.lock);
    return;
}

void vSetCheat2(void)
{
    xSemaphoreTake(my_player.lock, portMAX_DELAY);
    if (my_player.score1 > 9999) {
        my_player.score1 = 0;
        prints("Reseted player starting score.\n");
    } else {
        my_player.score1 = my_player.score1 + 500;
        prints("Raised player starting score!\n");
    }
    xSemaphoreGive(my_player.lock);
}

void vSetCheat3(void)
{
    int monster_delay;

    xQueuePeek(MonsterDelayQueue, &monster_delay, portMAX_DELAY);
    monster_delay = monster_delay + 5;
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
		if (buttons.buttons[KEYCODE(1)]) {
			if (!debounce_flags[0]) {
                debounce_flags[0] = 1;
                vSetCheat1();
            }
		} else {
            debounce_flags[0] = 0;
        }

        if (buttons.buttons[KEYCODE(2)]) {
			if (!debounce_flags[1]) {
                debounce_flags[1] = 1;
                vSetCheat2();
            }
		} else {
            debounce_flags[1] = 0;
        }

        if (buttons.buttons[KEYCODE(3)]) {
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

void vMenuDrawer(void *pvParameters)
{
	while (1) {
		xSemaphoreTake(DrawSignal, portMAX_DELAY);
		tumEventFetchEvents(FETCH_EVENT_BLOCK |
					FETCH_EVENT_NO_GL_CHECK);
		xGetButtonInput(); // Update global input

		xSemaphoreTake(ScreenLock, portMAX_DELAY);
		// Clear screen
		checkDraw(tumDrawClear(BACKGROUND_COLOUR),
				__FUNCTION__);
		vDrawMenuText();
		xSemaphoreGive(ScreenLock);

		vCheckKeyboardInput();
        vCheckCheatInput();
        vCheckCoinInput();
        vUpdateSavedValues();
	}
}

#define BULLET_CHANGE 3

void vUpdateBulletPosition(void)
{
    int i = 0, n_bullets;
    bullet_t my_bullet[MAX_OBJECTS];

    while (uxQueueMessagesWaiting(BulletQueue)) {
        xQueueReceive(BulletQueue, &my_bullet[i], portMAX_DELAY);
        if (my_bullet[i].type == SPACESHIP_BULLET)
            my_bullet[i].y = my_bullet[i].y - BULLET_CHANGE;
        if (my_bullet[i].type == MONSTER_BULLET || my_bullet[i].type == MOTHERGUNSHIP_BULLET)
            my_bullet[i].y = my_bullet[i].y + BULLET_CHANGE;
        i++;
    }

    n_bullets = i;
    for (i = 0; i < n_bullets; i++) {
        xQueueSend(BulletQueue, &my_bullet[i], portMAX_DELAY);
    }
}

void vShootBullet(int initial_x, int initial_y, int type)
{
    bullet_t my_bullet;

    my_bullet.x = initial_x;
    my_bullet.y = initial_y;
    my_bullet.width = 1;
    my_bullet.height = BULLET_HEIGHT;
    my_bullet.type = type;
    my_bullet.colour = White;

    xQueueSend(BulletQueue, &my_bullet, portMAX_DELAY);
}

#define TOP_COLISION 0
#define MONSTER_COLISION 1

void createColision(int bullet_x, int bullet_y, image_handle_t image_buffer)
{
    colision_t my_colision;

    my_colision.image = image_buffer;
    if (my_colision.image != NULL) {
        my_colision.width = tumDrawGetLoadedImageWidth(my_colision.image);
        my_colision.height = tumDrawGetLoadedImageHeight(my_colision.image);
    } else {
        my_colision.width = 0;
        my_colision.height = 0;
    }
    my_colision.x = bullet_x - my_colision.width / 2;
    my_colision.y = bullet_y - my_colision.height / 2;
    my_colision.frame_number = 0;

    xQueueSend(ColisionQueue, &my_colision, portMAX_DELAY);
}

void vCheckBulletColision(void)
{
    int k = 0, i, j, a, n_bullets;
    bullet_t my_bullet[MAX_OBJECTS];

    while (uxQueueMessagesWaiting(BulletQueue)) {
        xQueueReceive(BulletQueue, &my_bullet[k], portMAX_DELAY);
        if (my_bullet[k].y <= TOP_LINE_Y && my_bullet[k].type == SPACESHIP_BULLET) {//bullet exceeded top limit
            createColision(my_bullet[k].x, my_bullet[k].y, colision_image[0]);
            goto colision_detected;
        }


        for (i = 0; i < N_ROWS; i++) {
            for (j = 0; j < N_COLUMNS; j++) {
                if (my_bullet[k].y - BULLET_HEIGHT >= my_monsters.monster[i][j].y
                            && my_bullet[k].y <= my_monsters.monster[i][j].y + my_monsters.monster[i][j].height
                            && my_bullet[k].x >= my_monsters.monster[i][j].x
                            && my_bullet[k].x <= my_monsters.monster[i][j].x + my_monsters.monster[i][j].width
                            && my_monsters.monster[i][j].alive == 1 && my_bullet[k].type == SPACESHIP_BULLET) {
                    xSemaphoreTake(my_player.lock, portMAX_DELAY);//TALVEZ FAZER FUNÇÃO QUE DEIA REPLACE NESTE BLOCO PARA FICAR MAIS FACIL DE LER
                    if (my_monsters.monster[i][j].type == 0)
                        my_player.score1 = my_player.score1 + 30;
                    if (my_monsters.monster[i][j].type == 1)
                        my_player.score1 = my_player.score1 + 20;
                    if (my_monsters.monster[i][j].type == 2)
                        my_player.score1 = my_player.score1 + 10;
                    xSemaphoreGive(my_player.lock);
                    createColision(my_monsters.monster[i][j].x + my_monsters.monster[i][j].width / 2, 
                                            my_monsters.monster[i][j].y + my_monsters.monster[i][j].height / 2, colision_image[1]);
                    xSemaphoreTake(my_monsters.lock, portMAX_DELAY);
                    my_monsters.monster[i][j].alive = 0;
                    xSemaphoreGive(my_monsters.lock);
                    vUpdateMonsterDelay();
                    goto colision_detected;
                }
            }
        }


        if (my_bullet[k].y + BULLET_HEIGHT >= GREEN_LINE_Y 
                            && (my_bullet[k].type == MONSTER_BULLET || my_bullet[k].type == MOTHERGUNSHIP_BULLET)) {
            createColision(my_bullet[k].x, my_bullet[k].y + BULLET_HEIGHT, colision_image[0]);
            goto colision_detected;
        }

        if (my_bullet[k].y + BULLET_HEIGHT >= my_spaceship.y
                            && my_bullet[k].y <= my_spaceship.y + my_spaceship.height
                            && my_bullet[k].x >= my_spaceship.x
                            && my_bullet[k].x <= my_spaceship.x + my_spaceship.width
                            && (my_bullet[k].type == MONSTER_BULLET || my_bullet[k].type == MOTHERGUNSHIP_BULLET)) {
            xSemaphoreTake(my_player.lock, portMAX_DELAY);
            my_player.n_lives--;
            xSemaphoreGive(my_player.lock);
            createColision(my_spaceship.x + my_spaceship.width / 2, my_spaceship.y + my_spaceship.height / 2, colision_image[1]);
            vResetSpaceship();
            goto colision_detected;
        }

        if (my_bullet[k].y - BULLET_HEIGHT >= my_mothergunship.y
                            && my_bullet[k].y <= my_mothergunship.y + my_mothergunship.height
                            && my_bullet[k].x >= my_mothergunship.x
                            && my_bullet[k].x <= my_mothergunship.x + my_mothergunship.width
                            && my_mothergunship.alive == 1 
                            && my_bullet[k].type == SPACESHIP_BULLET) {
            vKillMothergunship();
            xSemaphoreTake(my_player.lock, portMAX_DELAY);
            my_player.score1 = my_player.score1 + 50 * (rand() % 4 + 1);
            xSemaphoreGive(my_player.lock);
            createColision(my_mothergunship.x + my_mothergunship.width / 2, my_mothergunship.y + my_mothergunship.height / 2, colision_image[1]);
            goto colision_detected;
        }

        for (a = 0; a < N_BUNKERS; a++) {
            for (i = 0; i < 2; i++) {
                for (j = 0; j < 3; j++) {
                    if (my_bunkers.bunker[a].component[i][j].damage < 3
                         && (my_bullet[k].y - BULLET_HEIGHT >= my_bunkers.bunker[a].component[i][j].y)
                         && my_bullet[k].y <= my_bunkers.bunker[a].component[i][j].y + my_bunkers.bunker[a].component[i][j].height
                         && my_bullet[k].x >= my_bunkers.bunker[a].component[i][j].x
                         && my_bullet[k].x <= my_bunkers.bunker[a].component[i][j].x + my_bunkers.bunker[a].component[i][j].width) {
                        xSemaphoreTake(my_bunkers.lock, portMAX_DELAY);
                        my_bunkers.bunker[a].component[i][j].damage++;
                        xSemaphoreGive(my_bunkers.lock);
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
        vTaskSuspend(GameDrawer);
        vTaskSuspend(MonsterBulletShooter);
        vTaskDelay(pdMS_TO_TICKS(1000));
        vResetGameBoard();
        vTaskResume(GameDrawer);
        vTaskResume(MonsterBulletShooter);
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
        vTaskSuspend(GameDrawer);
        vTaskSuspend(MonsterBulletShooter);
        vTaskDelay(pdMS_TO_TICKS(1000));
        vResetGameBoard();
        vResetPlayer();
        vTaskResume(GameDrawer);
        vTaskResume(MonsterBulletShooter);
    }
}

void vUpdateMothergunshipPosition(void)
{
    if (my_mothergunship.alive) {
        xSemaphoreTake(my_mothergunship.lock, portMAX_DELAY);
        if (my_mothergunship.direction == LEFT_TO_RIGHT)
            my_mothergunship.x = my_mothergunship.x + 1;
        if (my_mothergunship.direction == RIGHT_TO_LEFT)
            my_mothergunship.x = my_mothergunship.x - 1;
        if (my_mothergunship.x > SCREEN_WIDTH) {
            xSemaphoreGive(my_mothergunship.lock);
            goto reset_mothergunship;
        }
        xSemaphoreGive(my_mothergunship.lock);
    }

    return;

reset_mothergunship:
    vResetMothergunship();
    vKillMothergunship();
    return;
}

int vSpaceshipBulletActive(void)
{
    bullet_t my_bullet[MAX_OBJECTS];
    int bullet_active = 0, n_bullets, i = 0;

    while(uxQueueMessagesWaiting(BulletQueue)) {
        xQueueReceive(BulletQueue, &my_bullet[i], portMAX_DELAY);
        if (my_bullet[i].type == SPACESHIP_BULLET)
            bullet_active = 1;
        i++;
    }

    n_bullets = i;
    for (i = 0; i < n_bullets; i++) {
        xQueueSend(BulletQueue, &my_bullet[i], portMAX_DELAY);
    }

    return bullet_active;
}

void vGameLogic(void *pvParameters)
{
	while (1) {
        xGetButtonInput();
        vCheckKeyboardInput();
        vUpdateBulletPosition();
        vUpdateMothergunshipPosition();
        vCheckBulletColision();
        if (tumEventGetMouseLeft() == 1 && vSpaceshipBulletActive() == 0)
            vShootBullet(my_spaceship.x + my_spaceship.width / 2, my_spaceship.y, SPACESHIP_BULLET);
        vCheckMonstersDead();
        vCheckPlayerDead();
        vCheckPauseInput();
        vTaskDelay(pdMS_TO_TICKS(8));
	}
}

void vDrawBullets(void)
{
    int i = 0, n_bullets;
    bullet_t my_bullet[MAX_OBJECTS];

    while(uxQueueMessagesWaiting(BulletQueue)) {
        xQueueReceive(BulletQueue, &my_bullet[i], portMAX_DELAY);
        checkDraw(tumDrawFilledBox(my_bullet[i].x, my_bullet[i].y, my_bullet[i].width, my_bullet[i].height, my_bullet[i].colour), __FUNCTION__);
        i++;
    }

    n_bullets = i;
    for (i = 0; i < n_bullets; i++) {
        xQueueSend(BulletQueue, &my_bullet[i], portMAX_DELAY);
    }
}

void vDrawColisions(void)
{
    int i = 0, n_colisions;
    colision_t my_colision[MAX_OBJECTS];

    while(uxQueueMessagesWaiting(ColisionQueue)) {
        xQueueReceive(ColisionQueue, &my_colision[i], portMAX_DELAY);
        if (my_colision[i].image != NULL)
            checkDraw(tumDrawLoadedImage(my_colision[i].image, my_colision[i].x, my_colision[i].y), __FUNCTION__);
        my_colision[i].frame_number++;
        if (my_colision[i].frame_number > 20) {
            i--;
        }
        i++;
    }

    n_colisions = i;
    for (i = 0; i < n_colisions; i++) {
        xQueueSend(ColisionQueue, &my_colision[i], portMAX_DELAY);
    }
}

void vDrawMonsters(void)
{
    int i, j;

    for (i = 0; i < N_ROWS; i++) {
        for (j = 0; j < N_COLUMNS; j++) {
            if (my_monsters.monster[i][j].alive)
                checkDraw(tumDrawSprite(my_monsters.monster[i][j].spritesheet, my_monsters.monster[i][j].todraw, 0,
                            my_monsters.monster[i][j].x, my_monsters.monster[i][j].y), __FUNCTION__);
            
        }
    }
}

void vDrawBunkers(void)
{
    int k, i, j;

    for (k = 0; k < N_BUNKERS; k++) {
        for (i = 0; i < 2; i++) {
            for (j = 0; j < 3; j++) {
                if (my_bunkers.bunker[k].component[i][j].damage < 3) {
                    checkDraw(tumDrawLoadedImage(my_bunkers.bunker[k].component[i][j].image,
                                    my_bunkers.bunker[k].component[i][j].x,
                                    my_bunkers.bunker[k].component[i][j].y)
                                    , __FUNCTION__);
                }
            }
        }
    }
}

void vDrawGameObjects(void)
{
    checkDraw(tumDrawLoadedImage(my_spaceship.image, my_spaceship.x,
                                my_spaceship.y),
             __FUNCTION__);
    checkDraw(tumDrawFilledBox(0, GREEN_LINE_Y, SCREEN_WIDTH, 0, Green), __FUNCTION__);
    
    if (my_player.n_lives >= 2)
        checkDraw(tumDrawLoadedImage(my_spaceship.image, 55, LOWER_TEXT_YLOCATION + 5), __FUNCTION__);
    if (my_player.n_lives >= 3)
        checkDraw(tumDrawLoadedImage(my_spaceship.image, 55 + my_spaceship.width * 1.2, LOWER_TEXT_YLOCATION + 5), __FUNCTION__);
    
    vDrawMonsters();
    vDrawBunkers();
    vDrawBullets();
    vDrawColisions();

    if (my_mothergunship.alive)
        checkDraw(tumDrawLoadedImage(my_mothergunship.image, my_mothergunship.x, my_mothergunship.y), __FUNCTION__);
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

void vMonsterBulletShooter(void *pvParameters) {
    int shooter_column, shooter_row, i;

    while (1) {
        shooter_column = rand() % N_COLUMNS;
        
        for (i = N_ROWS - 1; i >= 0; i--) {
            if (my_monsters.monster[i][shooter_column].alive) {
                shooter_row = i;
                break;
            }
        }
        if (i == -1)
            continue;

        vShootBullet(my_monsters.monster[shooter_row][shooter_column].x + my_monsters.monster[shooter_row][shooter_column].width / 2,
                     my_monsters.monster[shooter_row][shooter_column].y + my_monsters.monster[shooter_row][shooter_column].height, MONSTER_BULLET);
        vTaskDelay(pdMS_TO_TICKS(2500));
    }
}

int vComputeRightMostMonster(int i)
{
    int right_index = -1, j;

    for (j = 0; j < N_COLUMNS; j++) {
        if (my_monsters.monster[i][j].alive)
            right_index = j;
    }

    return right_index;
}

int vComputeLeftMostMonster(int i)
{
    int left_index = -1, j;

    for (j = N_COLUMNS - 1; j >= 0; j--) {
        if (my_monsters.monster[i][j].alive)
            left_index = j;
    }

    return left_index;
}

void vMonsterMoveCloser(void)
{
    int i, j;

    if (xSemaphoreTake(my_monsters.lock, 0) == pdTRUE) {
        for (i = 0; i < N_ROWS; i++) {
            for (j = 0; j < N_COLUMNS; j++) {
                my_monsters.monster[i][j].y = my_monsters.monster[i][j].y + 10;
            }
        }
        xSemaphoreGive(my_monsters.lock);
    }
}

void vUpdateDirection(int *direction)
{
    int right_index, left_index, i;

    for (i = 0; i < N_ROWS; i++) {
        right_index = vComputeRightMostMonster(i);
        left_index = vComputeLeftMostMonster(i);
        if (right_index == -1 && left_index == -1)
            continue;
        if (my_monsters.monster[i][left_index].x < 10 
                            || my_monsters.monster[i][right_index].x 
                            + my_monsters.monster[i][right_index].width > SCREEN_WIDTH - 10) {
            (*direction) = -1 * (*direction);
            vMonsterMoveCloser();
            return;
        }
    }
}

void vMonsterMover(void *pvParameters)
{
    int i, j, direction = 1;

    TickType_t monster_delay;

    while (1) {
        for (i = N_ROWS - 1 ; i >= 0; i--) {
            for (j = 0; j < N_COLUMNS; j++) {
                if (xSemaphoreTake(my_monsters.lock, 0) == pdTRUE) {
                    if (my_monsters.monster[i][j].alive) {
                        my_monsters.monster[i][j].x = my_monsters.monster[i][j].x + direction * 5;
                        my_monsters.monster[i][j].todraw = !my_monsters.monster[i][j].todraw;
                        xSemaphoreGive(my_monsters.lock);
                    } else {
                        xSemaphoreGive(my_monsters.lock);
                        continue;
                    }
                }
                xQueuePeek(MonsterDelayQueue, &monster_delay, portMAX_DELAY);
                vTaskDelay(pdMS_TO_TICKS(monster_delay));
            }
        }
        vUpdateDirection(&direction);
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
        vDrawText("PAUSED", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2, CENTERING);
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
    mothergunship_image = tumDrawLoadImage("mothergunship.png");
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

void vSaveHighScore(void)
{
    FILE *fp = NULL;

    fp = fopen("highscore.txt", "w");
    fprintf(fp, "Last session's highscore was %d.\n", my_player.highscore);
    fclose(fp);
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

	//Timers
    xMothergunshipTimer = xTimerCreate("Mothergunship Timer", pdMS_TO_TICKS(ORIGINAL_TIMER), pdTRUE, (void *) 0, vMothergunshipTimerCallback);
    if (xMothergunshipTimer == NULL) {
        PRINT_ERROR("Could not open mothergunship timer");
        goto err_mothergunship_timer;
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

    if (xTaskCreate(vMonsterBulletShooter, "MonsterBulletShooter", STACK_SIZE,
			NULL, mainGENERIC_PRIORITY + 3, &MonsterBulletShooter) != pdPASS) {
		PRINT_TASK_ERROR("MonsterBulletShooter");
		goto err_monster_bullet_shooter;
	}

    if (xTaskCreate(vMonsterMover, "MonsterMover", STACK_SIZE,
			NULL, mainGENERIC_PRIORITY, &MonsterMover) != pdPASS) {
		PRINT_TASK_ERROR("MonsterMover");
		goto err_monster_mover;
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

    atexit(vSaveHighScore);

    vInitImages();
    vInitSpriteSheets();
    vInitPlayer();
    vInitSpaceship();
    vInitMonsters();
    vInitMonsterDelay();
    vInitMothergunship();
    vInitBunkers();
    vInitSavedValues();

    printf("Welcome to Space Invaders Remastered HD!\n");

	vTaskSuspender();

	vTaskStartScheduler();

	return EXIT_SUCCESS;

	vTaskDelete(PauseDrawer);
err_PauseDrawer:
	vTaskDelete(GameDrawer);
err_GameDrawer:
	vTaskDelete(MenuDrawer);
err_MenuDrawer:
    vTaskDelete(MonsterMover);
err_monster_mover:
    vTaskDelete(MonsterBulletShooter);
err_monster_bullet_shooter:
	vTaskDelete(GameLogic);
err_game_logic:
	vTaskDelete(BufferSwap);
err_bufferswap:
	vTaskDelete(StateMachine);
err_statemachine:
    xTimerDelete(xMothergunshipTimer, portMAX_DELAY);
err_mothergunship_timer:
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

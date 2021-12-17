#include <stdio.h>
#include <stdlib.h>

#include <SDL2/SDL_scancode.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"
#include "timers.h"
#include "string.h"
#include <math.h>

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

#define STATE_COUNT 2

#define MENU 0
#define GAME 1

#define NEXT_TASK 0
#define PREV_TASK 1

#define STARTING_STATE GAME

#define STATE_DEBOUNCE_DELAY 300
#define SHOOT_DEBOUNCE_DELAY 300

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

static TimerHandle_t xMothergunshipTimer;

static StaticTask_t GameDrawerBuffer;
static StackType_t xStack[STACK_SIZE];

static QueueHandle_t StateChangeQueue = NULL;
static QueueHandle_t CurrentStateQueue = NULL;
static QueueHandle_t BulletQueue = NULL;
static QueueHandle_t ColisionQueue = NULL;

static SemaphoreHandle_t DrawSignal = NULL;
static SemaphoreHandle_t ScreenLock = NULL;

static image_handle_t monster_image[3] = {NULL};
static image_handle_t mothergunship_image = NULL;

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
    image_handle_t image;

    int x; /**< X pixel coord of monster on screen */
    int y; /**< Y pixel coord of monster on screen */

    signed short width;
    signed short height;

    int type;
    int alive;

    callback_t callback; /**< monster callback */
    void *args; /**< monster callback args */
    SemaphoreHandle_t lock;
} monster_t;

static monster_t my_monster[N_ROWS][N_COLUMNS] = { 0 };

typedef struct player {
    int score1; /**< X pixel coord of monster on screen */
    int highscore; /**< Y pixel coord of monster on screen */
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

void vResetSpaceship(void)
{
    xSemaphoreTake(my_spaceship.lock, portMAX_DELAY);
    my_spaceship.x = SCREEN_WIDTH / 2 - my_spaceship.width / 2;
    xSemaphoreGive(my_spaceship.lock);
}

void vInitSpaceship(void)
{
    my_spaceship.image = tumDrawLoadImage("spaceship.png");
    my_spaceship.width = tumDrawGetLoadedImageWidth(my_spaceship.image);
    my_spaceship.height = tumDrawGetLoadedImageHeight(my_spaceship.image);
    my_spaceship.x = SCREEN_WIDTH / 2 - my_spaceship.width / 2;
    my_spaceship.y = SPACESHIP_Y;
}

void vResetMonsters(void)
{
    int h_spacing, v_spacing, i, j;

    h_spacing = tumDrawGetLoadedImageWidth(monster_image[2]);
    v_spacing = tumDrawGetLoadedImageHeight(monster_image[2]);

    for (i = 0; i < N_ROWS; i++) {
        for (j = 0; j < N_COLUMNS; j++) {
            xSemaphoreTake(my_monster[i][j].lock, portMAX_DELAY);
            my_monster[i][j].x = 15 + h_spacing * 1.5 * j;
            my_monster[i][j].y = SCREEN_HEIGHT / 4 + v_spacing * 2 * i;
            my_monster[i][j].alive = 1;
            xSemaphoreGive(my_monster[i][j].lock);
        }
    }
}

void vInitMonsters(void)
{
    int h_spacing, v_spacing, i, j;

    h_spacing = tumDrawGetLoadedImageWidth(monster_image[2]);
    v_spacing = tumDrawGetLoadedImageHeight(monster_image[2]);

    for (i = 0; i < N_ROWS; i++) {
        for (j = 0; j < N_COLUMNS; j++) {
            if (i == 0) {
                my_monster[i][j].type = 0;
                my_monster[i][j].image = monster_image[0];
            }
            if (i == 1 || i == 2) {
                my_monster[i][j].type = 1;
                my_monster[i][j].image = monster_image[1];
            }
            if (i == 3 || i == 4) {
                my_monster[i][j].type = 2;
                my_monster[i][j].image = monster_image[2];
            }
            my_monster[i][j].width = tumDrawGetLoadedImageWidth(my_monster[i][j].image);
            my_monster[i][j].height = tumDrawGetLoadedImageHeight(my_monster[i][j].image);
            my_monster[i][j].x = 15 + h_spacing * 1.5 * j;
            my_monster[i][j].y = SCREEN_HEIGHT / 4 + v_spacing * 2 * i;
            my_monster[i][j].alive = 1;
            my_monster[i][j].lock = xSemaphoreCreateMutex();
        }
    }
}

void vInitPlayer(void)
{
    my_player.score1 = 0;
    my_player.score2 = 0;
    my_player.n_lives = 3;
    my_player.credits = 0;
    my_player.lock = xSemaphoreCreateMutex();
}

#define LEFT_TO_RIGHT 0
#define RIGHT_TO_LEFT 1

void vResetMothergunship(void)
{
    xSemaphoreTake(my_mothergunship.lock, portMAX_DELAY);
    my_mothergunship.direction = !my_mothergunship.direction;
    if (my_mothergunship.direction == LEFT_TO_RIGHT)
        my_mothergunship.x = -1 * my_mothergunship.width;
    if (my_mothergunship.direction == RIGHT_TO_LEFT)
        my_mothergunship.x = SCREEN_WIDTH;
    my_mothergunship.alive = 1;
    xSemaphoreGive(my_mothergunship.lock);
}

void vMothergunshipTimerCallback(TimerHandle_t xMothergunshipTimer)
{
    vResetMothergunship();
}

void vInitMothergunship(void)
{
    my_mothergunship.image = mothergunship_image;
    my_mothergunship.x = 0;
    my_mothergunship.y = MOTHERGUNSHIP_Y;
    my_mothergunship.width = tumDrawGetLoadedImageWidth(my_mothergunship.image);
    my_mothergunship.height = tumDrawGetLoadedImageHeight(my_mothergunship.image);
    my_mothergunship.alive = 0;
    my_mothergunship.direction = RIGHT_TO_LEFT;
    my_mothergunship.lock = xSemaphoreCreateMutex();
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

static int vCheckStateInput(void)
{
	if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
		if (buttons.buttons[KEYCODE(M)]) {
			buttons.buttons[KEYCODE(M)] = 0;
			if (StateChangeQueue) {
				xSemaphoreGive(buttons.lock);
				xQueueSend(StateChangeQueue, &next_state_signal,
					   0);
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
}

void basicSequentialStateMachine(void *pvParameters)
{
	unsigned char current_state = STARTING_STATE; // Default state
	unsigned char state_changed =
		1; // Only re-evaluate state if it has changed
	unsigned char input = 0;

	const int state_change_period = STATE_DEBOUNCE_DELAY;

	TickType_t last_change = xTaskGetTickCount();

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
				if (MenuDrawer) {
					vTaskResume(MenuDrawer);
				}
				break;
			case GAME:
                xTimerReset(xMothergunshipTimer, portMAX_DELAY);
				if (GameDrawer) {
					vTaskResume(GameDrawer);
				}
				if (GameLogic) {
					vTaskResume(GameLogic);
                }
                if (MonsterBulletShooter) {
					vTaskResume(MonsterBulletShooter);
                }
				break;
			default:
				break;
			}
			state_changed = 0;
		}
	}
}

#define FPS_AVERAGE_COUNT 50
#define FPS_FONT "IBMPlexSans-Bold.ttf"

void vDrawFPS(void)
{
	static unsigned int periods[FPS_AVERAGE_COUNT] = { 0 };
	static unsigned int periods_total = 0;
	static unsigned int index = 0;
	static unsigned int average_count = 0;
	static TickType_t xLastWakeTime = 0, prevWakeTime = 0;
	static char str[10] = { 0 };
	static int text_width;
	int fps = 0;
	font_handle_t cur_font = tumFontGetCurFontHandle();

	if (average_count < FPS_AVERAGE_COUNT) {
		average_count++;
	} else {
		periods_total -= periods[index];
	}

	xLastWakeTime = xTaskGetTickCount();

	if (prevWakeTime != xLastWakeTime) {
		periods[index] =
			configTICK_RATE_HZ / (xLastWakeTime - prevWakeTime);
		prevWakeTime = xLastWakeTime;
	} else {
		periods[index] = 0;
	}

	periods_total += periods[index];

	if (index == (FPS_AVERAGE_COUNT - 1)) {
		index = 0;
	} else {
		index++;
	}

	fps = periods_total / average_count;

	tumFontSelectFontFromName(FPS_FONT);

	sprintf(str, "FPS: %2d", fps);

	if (!tumGetTextSize((char *)str, &text_width, NULL))
		checkDraw(tumDrawFilledBox(SCREEN_WIDTH - text_width - 10,
					   SCREEN_HEIGHT -
						   DEFAULT_FONT_SIZE * 1.5,
					   text_width, DEFAULT_FONT_SIZE,
					   BACKGROUND_COLOUR),
			  __FUNCTION__);
	checkDraw(tumDrawText(str, SCREEN_WIDTH - text_width - 10,
			      SCREEN_HEIGHT - DEFAULT_FONT_SIZE * 1.5,
			      TEXT_COLOUR),
		  __FUNCTION__);

	tumFontSelectFontFromHandle(cur_font);
	tumFontPutFontHandle(cur_font);
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

#define CHANGE_IN_POSITION 3

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
	unsigned char current_state;

	if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
		if (buttons.buttons[key]) {
			xSemaphoreGive(buttons.lock);
			if (xQueuePeek(CurrentStateQueue, &current_state, 0) ==
			    pdTRUE) {
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
    vDrawText("PLAY", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 4, CENTERING);
    vDrawText("SPACE INVADERS", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 3, CENTERING);
    vDrawText("SCORE ADVANCE TABLE", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2, CENTERING);
    checkDraw(tumDrawLoadedImage(mothergunship_image, SCREEN_WIDTH / 5 - 5, SCREEN_HEIGHT / 2 + DEFAULT_FONT_SIZE * 1.5 - 10), __FUNCTION__);
    vDrawText("=? MYSTERY", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 + DEFAULT_FONT_SIZE * 1.5, CENTERING);
    checkDraw(tumDrawLoadedImage(monster_image[0], SCREEN_WIDTH / 4 + 3, SCREEN_HEIGHT / 2 + DEFAULT_FONT_SIZE * 3 - 5), __FUNCTION__);
    vDrawText("=30 POINTS", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 + DEFAULT_FONT_SIZE * 3, CENTERING);
    checkDraw(tumDrawLoadedImage(monster_image[1], SCREEN_WIDTH / 4, SCREEN_HEIGHT / 2 + DEFAULT_FONT_SIZE * 4.5 - 5), __FUNCTION__);
    vDrawText("=20 POINTS", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 + DEFAULT_FONT_SIZE * 4.5, CENTERING);
    checkDraw(tumDrawLoadedImage(monster_image[2], SCREEN_WIDTH / 4, SCREEN_HEIGHT / 2 + DEFAULT_FONT_SIZE * 6 - 5), __FUNCTION__);
    vDrawText("=10 POINTS", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 + DEFAULT_FONT_SIZE * 6, CENTERING);
    vDrawCredit();
}

void vMenuDrawer(void *pvParameters)
{
	prints("Menu Init'd\n");

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
	}
}

void vUpdateBulletPosition(void)
{
    int i = 0, n_bullets;
    bullet_t my_bullet[10];

    while (uxQueueMessagesWaiting(BulletQueue)) {
        xQueueReceive(BulletQueue, &my_bullet[i], portMAX_DELAY);
        if (my_bullet[i].type == SPACESHIP_BULLET)
            my_bullet[i].y = my_bullet[i].y - 5;
        if (my_bullet[i].type == MONSTER_BULLET || my_bullet[i].type == MOTHERGUNSHIP_BULLET)
            my_bullet[i].y = my_bullet[i].y + 5;
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

void createColision(int bullet_x, int bullet_y, int colision_type)
{
    colision_t my_colision;

    if (colision_type == TOP_COLISION) {
        my_colision.image = tumDrawLoadImage("colision.png");
    }
    if (colision_type == MONSTER_COLISION) {
        my_colision.image = tumDrawLoadImage("colision2.png");
    }
    my_colision.width = tumDrawGetLoadedImageWidth(my_colision.image);
    my_colision.height = tumDrawGetLoadedImageHeight(my_colision.image);
    my_colision.x = bullet_x - my_colision.width / 2;
    my_colision.y = bullet_y - my_colision.height / 2;
    my_colision.frame_number = 0;

    xQueueSend(ColisionQueue, &my_colision, portMAX_DELAY);
}

void vCheckBulletColision(void)
{
    int k = 0, i, j, n_bullets;
    bullet_t my_bullet[10];

    while (uxQueueMessagesWaiting(BulletQueue)) {
        xQueueReceive(BulletQueue, &my_bullet[k], portMAX_DELAY);
        if (my_bullet[k].y <= TOP_LINE_Y && my_bullet[k].type == SPACESHIP_BULLET) {//bullet exceeded top limit
            createColision(my_bullet[k].x, my_bullet[k].y, TOP_COLISION);
            goto colision_detected;
        }

        for (i = 0; i < N_ROWS; i++) {
            for (j = 0; j < N_COLUMNS; j++) {
                if (my_bullet[k].y - BULLET_HEIGHT >= my_monster[i][j].y
                            && my_bullet[k].y <= my_monster[i][j].y + my_monster[i][j].height
                            && my_bullet[k].x >= my_monster[i][j].x
                            && my_bullet[k].x <= my_monster[i][j].x + my_monster[i][j].width
                            && my_monster[i][j].alive == 1 && my_bullet[k].type == SPACESHIP_BULLET) {
                    xSemaphoreTake(my_player.lock, portMAX_DELAY);//TALVEZ FAZER FUNÇÃO QUE DEIA REPLACE NESTE BLOCO PARA FICAR MAIS FACIL DE LER
                    if (my_monster[i][j].type == 0)
                        my_player.score1 = my_player.score1 + 30;
                    if (my_monster[i][j].type == 1)
                        my_player.score1 = my_player.score1 + 20;
                    if (my_monster[i][j].type == 2)
                        my_player.score1 = my_player.score1 + 10;
                    xSemaphoreGive(my_player.lock);
                    createColision(my_monster[i][j].x + my_monster[i][j].width / 2, 
                                            my_monster[i][j].y + my_monster[i][j].height / 2, MONSTER_COLISION);
                    xSemaphoreTake(my_monster[i][j].lock, portMAX_DELAY);
                    my_monster[i][j].alive = 0;
                    xSemaphoreGive(my_monster[i][j].lock);
                    goto colision_detected;
                }
            }
        }

        if (my_bullet[k].y + BULLET_HEIGHT >= GREEN_LINE_Y 
                            && (my_bullet[k].type == MONSTER_BULLET || my_bullet[k].type == MOTHERGUNSHIP_BULLET)) {
            createColision(my_bullet[k].x, my_bullet[k].y + BULLET_HEIGHT, TOP_COLISION);
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
            createColision(my_spaceship.x + my_spaceship.width / 2, my_spaceship.y + my_spaceship.height / 2, MONSTER_COLISION);
            vResetSpaceship();
            goto colision_detected;
        }

        if (my_bullet[k].y - BULLET_HEIGHT >= my_mothergunship.y
                            && my_bullet[k].y <= my_mothergunship.y + my_mothergunship.height
                            && my_bullet[k].x >= my_mothergunship.x
                            && my_bullet[k].x <= my_mothergunship.x + my_mothergunship.width
                            && my_mothergunship.alive == 1 
                            && my_bullet[k].type == SPACESHIP_BULLET) {
            xSemaphoreTake(my_mothergunship.lock, portMAX_DELAY);
            my_mothergunship.alive = 0;
            xSemaphoreGive(my_mothergunship.lock);
            xSemaphoreTake(my_player.lock, portMAX_DELAY);
            my_player.score1 = my_player.score1 + 50;
            xSemaphoreGive(my_player.lock);
            createColision(my_mothergunship.x + my_mothergunship.width / 2, my_mothergunship.y + my_mothergunship.height / 2, MONSTER_COLISION);
            goto colision_detected;
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
            if (my_monster[i][j].alive)
                n_monster_alive++;
        }
    }

    if (!n_monster_alive)
        vResetMonsters();
}

void vCheckPlayerDead(void)
{
    if (!my_player.n_lives) {
        xSemaphoreTake(my_player.lock, portMAX_DELAY);
        my_player.n_lives = 3;
        if (my_player.score1 > my_player.highscore) {
            my_player.highscore = my_player.score1;
        }
        my_player.score1 = 0;
        vResetMonsters();
        xTimerReset(xMothergunshipTimer, portMAX_DELAY);
        vInitMothergunship();
        xSemaphoreGive(my_player.lock);
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
            my_mothergunship.alive = 0;
            xTimerReset(xMothergunshipTimer, portMAX_DELAY);
        }
        xSemaphoreGive(my_mothergunship.lock);
    }
}

void vGameLogic(void *pvParameters)
{
    TickType_t last_shot = xTaskGetTickCount();

	while (1) {
        xGetButtonInput();
        vCheckKeyboardInput();
        vUpdateBulletPosition();
        vUpdateMothergunshipPosition();
        vCheckBulletColision();
        vCheckMonstersDead();
        vCheckPlayerDead();
        if (tumEventGetMouseLeft() == 1 && xTaskGetTickCount() - last_shot >
            SHOOT_DEBOUNCE_DELAY) {
            vShootBullet(my_spaceship.x + my_spaceship.width / 2, my_spaceship.y, SPACESHIP_BULLET);
            last_shot = xTaskGetTickCount();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
	}
}

void vDrawBullets(void)
{
    int i = 0, n_bullets;
    bullet_t my_bullet[10];

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
    colision_t my_colision[10];

    while(uxQueueMessagesWaiting(ColisionQueue)) {
        xQueueReceive(ColisionQueue, &my_colision[i], portMAX_DELAY);
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
            if (my_monster[i][j].alive)
                checkDraw(tumDrawLoadedImage(my_monster[i][j].image, my_monster[i][j].x, my_monster[i][j].y), __FUNCTION__);
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
    vDrawBullets();
    vDrawColisions();

    if (my_mothergunship.alive)
        checkDraw(tumDrawLoadedImage(my_mothergunship.image, my_mothergunship.x, my_mothergunship.y), __FUNCTION__);
}

void vGameDrawer(void *pvParameters)
{
	prints("Game init'd\n");

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
    TickType_t xLastShot = xTaskGetTickCount();
    int shooter_column, shooter_row, i;

    while (1) {
        shooter_column = rand() % N_COLUMNS;
        
        for (i = N_ROWS - 1; i >= 0; i--) {
            if (my_monster[i][shooter_column].alive) {
                shooter_row = i;
                break;
            }
        }
        if (i == -1)
            continue;

        vShootBullet(my_monster[shooter_row][shooter_column].x + my_monster[shooter_row][shooter_column].width / 2,
                     my_monster[shooter_row][shooter_column].y + my_monster[shooter_row][shooter_column].height, MONSTER_BULLET);
        vTaskDelayUntil(&xLastShot, pdMS_TO_TICKS(2500));
    }
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

	//Load a second font for fun
	tumFontLoadFont(FPS_FONT, DEFAULT_FONT_SIZE);

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

    my_spaceship.lock = xSemaphoreCreateMutex();
    if (!my_spaceship.lock) {
		PRINT_ERROR("Failed to create spaceship lock");
		goto err_spaceship_lock;
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
		xQueueCreate(10, sizeof(bullet_t));
	if (!BulletQueue) {
		PRINT_ERROR("Could not open bullet queue");
		goto err_bullet_queue;
	}

    ColisionQueue =
		xQueueCreate(10, sizeof(colision_t));
	if (!ColisionQueue) {
		PRINT_ERROR("Could not open colision queue");
		goto err_colision_queue;
	}

	//Timers
    xMothergunshipTimer = xTimerCreate("Mothergunship Timer", pdMS_TO_TICKS(10000), pdTRUE, (void *) 0, vMothergunshipTimerCallback);
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

	//Normal Tasks
	if (xTaskCreate(vMenuDrawer, "MenuDrawer", STACK_SIZE,
			NULL, mainGENERIC_PRIORITY + 1,
			&MenuDrawer) != pdPASS) {
		PRINT_TASK_ERROR("MenuDrawer");
		goto err_MenuDrawer;
	}

	GameDrawer = xTaskCreateStatic(vGameDrawer, "Task2", STACK_SIZE, NULL,
				       mainGENERIC_PRIORITY + 1, xStack,
				       &GameDrawerBuffer);
	if (GameDrawer == NULL) {
		PRINT_TASK_ERROR("GameDrawer");
		goto err_GameDrawer;
	}

    monster_image[0] = tumDrawLoadImage("monster1.png");
    monster_image[1] = tumDrawLoadImage("monster3.png");
    monster_image[2] = tumDrawLoadImage("monster5.png");
    mothergunship_image = tumDrawLoadImage("mothergunship.png");

    srand(1);

    vInitPlayer();
    vInitSpaceship();
    vInitMonsters();
    vInitMothergunship();
	vTaskSuspender();

	vTaskStartScheduler();

	return EXIT_SUCCESS;

	vTaskDelete(GameDrawer);
err_GameDrawer:
	vTaskDelete(MenuDrawer);
err_MenuDrawer:
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
    vQueueDelete(ColisionQueue);
err_colision_queue:
    vQueueDelete(BulletQueue);
err_bullet_queue:
	vQueueDelete(CurrentStateQueue);
err_current_state_queue:
	vQueueDelete(StateChangeQueue);
err_state_change_queue:
    vSemaphoreDelete(my_spaceship.lock);
err_spaceship_lock:
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

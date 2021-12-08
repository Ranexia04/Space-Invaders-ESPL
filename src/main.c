#include <math.h>
#include <stdio.h>
#include <stdlib.h>

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

#define STATE_COUNT 2

#define MENU 0
#define GAME 1

#define NEXT_TASK 0
#define PREV_TASK 1

#define STARTING_STATE GAME

#define STATE_DEBOUNCE_DELAY 300

#define KEYCODE(CHAR) SDL_SCANCODE_##CHAR
#define BACKGROUND_COLOUR Black
#define TEXT_COLOUR White
#define OBJECT_COLOUR Green
#define PI 3.142857
#define FREQ 1
#define RADIUS 40
#define LOGO_FILENAME "freertos.jpg"
#define FPS_AVERAGE_COUNT 50
#define FPS_FONT "IBMPlexSans-Bold.ttf"

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

//Timers here if needed

static StaticTask_t GameDrawerBuffer;
static StackType_t xStack[STACK_SIZE];

static QueueHandle_t StateChangeQueue = NULL;
static QueueHandle_t CurrentStateQueue = NULL;

static SemaphoreHandle_t DrawSignal = NULL;
static SemaphoreHandle_t ScreenLock = NULL;

static image_handle_t logo_image = NULL;

typedef struct buttons_buffer {
	unsigned char buttons[SDL_NUM_SCANCODES];
	SemaphoreHandle_t lock;
} buttons_buffer_t;

static buttons_buffer_t buttons = { 0 };

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

void vDrawLogo(void)
{
	static int image_height;

	if ((image_height = tumDrawGetLoadedImageHeight(logo_image)) != -1)
		checkDraw(tumDrawLoadedImage(logo_image, 10,
					     SCREEN_HEIGHT - 10 - image_height),
			  __FUNCTION__);
	else {
		fprints(stderr,
			"Failed to get size of image '%s', does it exist?\n",
			LOGO_FILENAME);
	}
}

void vDrawGameWords(void)
{
    char str[30];
	int text_width;

    sprintf(str, "S C O R E < 1 >");
	text_width = tumGetTextSize(str, &text_width, NULL);
	checkDraw(tumDrawText(str, 10, 10, TEXT_COLOUR), __FUNCTION__);

    sprintf(str, "H I - S C O R E");
	text_width = tumGetTextSize(str, &text_width, NULL);
	checkDraw(tumDrawText(str, SCREEN_WIDTH / 3 + 10, 10, TEXT_COLOUR), __FUNCTION__);

    sprintf(str, "S C O R E < 2 >");
	text_width = tumGetTextSize(str, &text_width, NULL);
	checkDraw(tumDrawText(str, SCREEN_WIDTH * 2 / 3 + 10, 10, TEXT_COLOUR), __FUNCTION__);

    sprintf(str, "C R E D I T");
	text_width = tumGetTextSize(str, &text_width, NULL);
	checkDraw(tumDrawText(str, SCREEN_WIDTH * 2 / 3 - 15, SCREEN_HEIGHT - DEFAULT_FONT_SIZE - 20, TEXT_COLOUR), __FUNCTION__);
}

void vGetNumberString(char *str, int number, int n_digits)
{
    char num_str[10];
	int index = 0, i;
    
    sprintf(num_str, "%d", number);
    while ( num_str[index] != '\0' ) {
        index++;
    }
    index--;//index is now the index of the last digit of the number we want to draw
    
    i = 2 * n_digits - 1;
    str[i] = '\0';
    i = i - 2;
    while (i >= 0) {
        str[i] = ' ';
        i = i - 2;
    }

    i = 2 * n_digits - 2;
    while ( index >= 0 ) {
        str[i] = num_str[index];
        index--;
        i = i - 2;
    }
    while ( i >= 0 ) {
        str[i] = '0';
        i = i - 2;
    }
}

void vDrawGameNumbers(void)
{
    char str[30];
    int text_width, score1 = 0, highscore = 0, credit = 0, n_lives = 3;

    if ( score1 > 9999 ) {
        sprintf(str, "9 9 9 9");
    } else {
        vGetNumberString(str, score1, 4);
    }
	text_width = tumGetTextSize(str, &text_width, NULL);
	checkDraw(tumDrawText(str, 45, 15 + DEFAULT_FONT_SIZE, TEXT_COLOUR), __FUNCTION__);

    if ( highscore > 9999 ) {
        sprintf(str, "9 9 9 9");
    } else {
        vGetNumberString(str, highscore, 4);
    }
	text_width = tumGetTextSize(str, &text_width, NULL);
	checkDraw(tumDrawText(str, SCREEN_WIDTH / 3 + 25, 15 + DEFAULT_FONT_SIZE, TEXT_COLOUR), __FUNCTION__);

    if ( credit > 99 ) {
        sprintf(str, "9 9");
    } else {
        vGetNumberString(str, credit, 2);
    }
	text_width = tumGetTextSize(str, &text_width, NULL);
	checkDraw(tumDrawText(str, SCREEN_WIDTH - 50, SCREEN_HEIGHT - DEFAULT_FONT_SIZE - 20, TEXT_COLOUR), __FUNCTION__);

    vGetNumberString(str, n_lives, 1);
    text_width = tumGetTextSize(str, &text_width, NULL);
	checkDraw(tumDrawText(str, 20, SCREEN_HEIGHT - DEFAULT_FONT_SIZE - 20, TEXT_COLOUR), __FUNCTION__);
}

void vDrawGameText(void)
{
	vDrawGameWords();
    vDrawGameNumbers();
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
}

void basicSequentialStateMachine(void *pvParameters)
{
	unsigned char current_state = STARTING_STATE; // Default state
	unsigned char state_changed =
		1; // Only re-evaluate state if it has changed
	unsigned char input = 0;

	const int state_change_period = STATE_DEBOUNCE_DELAY;

	TickType_t last_change = xTaskGetTickCount();

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
				if (GameDrawer) {
					vTaskResume(GameDrawer);
				}
				if (GameLogic) {
					vTaskResume(GameLogic);
				}
				break;
			default:
				break;
			}
			state_changed = 0;
		}
	}
}

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

int vCheckButtonInput(int key)
{
	unsigned char current_state;

	if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
		if (buttons.buttons[key]) {
			buttons.buttons[key] = 0;
			xSemaphoreGive(buttons.lock);

			if (xQueuePeek(CurrentStateQueue, &current_state, 0) ==
			    pdTRUE) {
				switch (key) {
				case KEYCODE(1):

					break;
				case KEYCODE(2):

					break;
				case KEYCODE(3):

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
	vCheckButtonInput(KEYCODE(1));
	vCheckButtonInput(KEYCODE(2));
	vCheckButtonInput(KEYCODE(3));
}

void vMenuDrawer(void *pvParameters)
{
	prints("Menu Init'd\n");

	while (1) {
		if (DrawSignal)
			if (xSemaphoreTake(DrawSignal, portMAX_DELAY) ==
			    pdTRUE) {
				tumEventFetchEvents(FETCH_EVENT_BLOCK |
						    FETCH_EVENT_NO_GL_CHECK);
				xGetButtonInput(); // Update global input

				xSemaphoreTake(ScreenLock, portMAX_DELAY);

				// Clear screen
				checkDraw(tumDrawClear(BACKGROUND_COLOUR),
					  __FUNCTION__);
				checkDraw(tumDrawText("Menu", 20, 20, TEXT_COLOUR), __FUNCTION__);

				xSemaphoreGive(ScreenLock);

				vCheckKeyboardInput();
			}
	}
}

void vGameLogic(void *pvParameters)
{
	while (1) {
	}
}

void vGameDrawer(void *pvParameters)
{
	prints("Game init'd\n");

	while (1) {
		xSemaphoreTake(DrawSignal, portMAX_DELAY);
		tumEventFetchEvents(FETCH_EVENT_BLOCK |
				    FETCH_EVENT_NO_GL_CHECK);
		xGetButtonInput(); // Update global input

		xSemaphoreTake(ScreenLock, portMAX_DELAY);
		checkDraw(tumDrawClear(BACKGROUND_COLOUR), __FUNCTION__);
		vDrawGameText();

		xSemaphoreGive(ScreenLock);

		vCheckKeyboardInput();
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

	logo_image = tumDrawLoadImage(LOGO_FILENAME);

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

	//Timers

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
			NULL, mainGENERIC_PRIORITY, &GameLogic) != pdPASS) {
		PRINT_TASK_ERROR("GameLogic");
		goto err_game_logic;
	}

	//Normal Tasks
	if (xTaskCreate(vMenuDrawer, "MenuDrawer", STACK_SIZE,
			NULL, mainGENERIC_PRIORITY + 3,
			&MenuDrawer) != pdPASS) {
		PRINT_TASK_ERROR("MenuDrawer");
		goto err_MenuDrawer;
	}

	GameDrawer = xTaskCreateStatic(vGameDrawer, "Task2", STACK_SIZE, NULL,
				       mainGENERIC_PRIORITY + 2, xStack,
				       &GameDrawerBuffer);
	if (GameDrawer == NULL) {
		PRINT_TASK_ERROR("GameDrawer");
		goto err_GameDrawer;
	}

	vTaskSuspender();

	vTaskStartScheduler();

	return EXIT_SUCCESS;

	vTaskDelete(GameDrawer);
err_GameDrawer:
	vTaskDelete(MenuDrawer);
err_MenuDrawer:
	vTaskDelete(GameLogic);
err_game_logic:
	vTaskDelete(BufferSwap);
err_bufferswap:
	vTaskDelete(StateMachine);
err_statemachine:
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

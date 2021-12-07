#include <math.h>
#include <stdio.h>
#include <stdlib.h>

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

//#include "AsyncIO.h"

#define mainGENERIC_PRIORITY (tskIDLE_PRIORITY)
#define mainGENERIC_STACK_SIZE ((unsigned short)2560)
#define STACK_SIZE mainGENERIC_STACK_SIZE * 2

#define STATE_QUEUE_LENGTH 1

#define STATE_COUNT 1

#define MENU 1
#define GAME 0
//#define STATE_THREE 2

#define NEXT_TASK 0
#define PREV_TASK 1

#define STARTING_STATE GAME

#define STATE_DEBOUNCE_DELAY 300

#define KEYCODE(CHAR) SDL_SCANCODE_##CHAR
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


static StaticTask_t GameDrawerBuffer;
static StackType_t xStack[ STACK_SIZE ];

static QueueHandle_t StateQueue = NULL;
static QueueHandle_t CurrentStateQueue = NULL;

static QueueHandle_t InitTickQueue = NULL;
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
            }
            else {
                (*state)++;
            }
            break;
        case PREV_TASK:
            if (*state == 0) {
                *state = STATE_COUNT - 1;
            }
            else {
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
            if (StateQueue) {
                xSemaphoreGive(buttons.lock);
                xQueueSend(StateQueue, &next_state_signal, 0);
                return 0;
            }
            xSemaphoreGive(buttons.lock);
            return -1;
        }
        xSemaphoreGive(buttons.lock);
    }

    return 0;
}

void vDrawHelpText(void)
{
    static char str[100] = { 0 };
    static int text_width;
    ssize_t prev_font_size = tumFontGetCurFontSize();

    tumFontSetSize((ssize_t)30);

    sprintf(str, "[Q]uit");

    if (!tumGetTextSize((char *)str, &text_width, NULL))
        checkDraw(tumDrawText(str, SCREEN_WIDTH - text_width - 10,
                              DEFAULT_FONT_SIZE * 0.5, Black),
                  __FUNCTION__);

    tumFontSetSize(prev_font_size);
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

void vDrawStaticItems(void)
{
    vDrawHelpText();
    vDrawLogo();
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
    TickType_t tickstate3;

    while (1) {
        if (state_changed) {
            goto initial_state;
        }

        // Handle state machine input
        if (StateQueue)
            if (xQueueReceive(StateQueue, &input, portMAX_DELAY) ==
                pdTRUE)
                if (xTaskGetTickCount() - last_change >
                    state_change_period) {
                    changeState(&current_state, input);
                    xQueueOverwrite(CurrentStateQueue, &current_state);
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
    }
    else {
        periods_total -= periods[index];
    }

    xLastWakeTime = xTaskGetTickCount();

    if (prevWakeTime != xLastWakeTime) {
        periods[index] =
            configTICK_RATE_HZ / (xLastWakeTime - prevWakeTime);
        prevWakeTime = xLastWakeTime;
    }
    else {
        periods[index] = 0;
    }

    periods_total += periods[index];

    if (index == (FPS_AVERAGE_COUNT - 1)) {
        index = 0;
    }
    else {
        index++;
    }

    fps = periods_total / average_count;

    tumFontSelectFontFromName(FPS_FONT);

    sprintf(str, "FPS: %2d", fps);

    if (!tumGetTextSize((char *)str, &text_width, NULL))
        checkDraw(tumDrawFilledBox(SCREEN_WIDTH - text_width - 10, SCREEN_HEIGHT - DEFAULT_FONT_SIZE * 1.5,
            text_width, DEFAULT_FONT_SIZE, White), __FUNCTION__);
        checkDraw(tumDrawText(str, SCREEN_WIDTH - text_width - 10,
                              SCREEN_HEIGHT - DEFAULT_FONT_SIZE * 1.5,
                              Skyblue),
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
        vTaskDelayUntil(&xLastWakeTime,
            pdMS_TO_TICKS(frameratePeriod));
    }
}

int vCheckButtonInput(int key)
{
    unsigned char current_state, Incrementing_state;

    if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
        if (buttons.buttons[key]) {
            buttons.buttons[key] = 0;
            xSemaphoreGive(buttons.lock);

            if (xQueuePeek(CurrentStateQueue, &current_state, 0) == pdTRUE) {
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
    ball_t *my_circle = createBall(SCREEN_WIDTH / 4, SCREEN_HEIGHT / 2, Red,
                                 RADIUS, 1000, NULL, NULL);

    static char str[100];
    static int str_width = 0;

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
                checkDraw(tumDrawClear(Black), __FUNCTION__);
                vDrawStaticItems();

                // Draw FPS in lower right corner
                vDrawFPS();

                xSemaphoreGive(ScreenLock);

                vCheckKeyboardInput();
            }
    }
}

void vGameLogic(void *pvParameters)
{
    static int* write_circle = NULL;
    write_circle = (int*)calloc(2, sizeof(int));
    static unsigned char write_flags[2] = {0};

    TickType_t xLastWakeTime;
    xLastWakeTime = xTaskGetTickCount();

    while (1) {
        
    }
}

void vGameDrawer(void *pvParameters)
{
    ball_t *my_circle = createBall(SCREEN_WIDTH / 4, SCREEN_HEIGHT / 2, Red,
                                 RADIUS, 1000, NULL, NULL);

    prints("Game init'd\n");


    while (1) {
        xSemaphoreTake(DrawSignal, portMAX_DELAY);
        tumEventFetchEvents(FETCH_EVENT_BLOCK |
                                    FETCH_EVENT_NO_GL_CHECK);
        xGetButtonInput(); // Update global input

        

        xSemaphoreTake(ScreenLock, portMAX_DELAY);
        checkDraw(tumDrawClear(Black), __FUNCTION__);
        vDrawStaticItems();

        

        vDrawFPS();
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
    }
    else {
        prints("drawing");
    }

    if (tumEventInit()) {
        PRINT_ERROR("Failed to initialize events");
        goto err_init_events;
    }
    else {
        prints(", events");
    }

    if (tumSoundInit(bin_folder_path)) {
        PRINT_ERROR("Failed to initialize audio");
        goto err_init_audio;
    }
    else {
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
    StateQueue = xQueueCreate(STATE_QUEUE_LENGTH, sizeof(unsigned char));
    if (!StateQueue) {
        PRINT_ERROR("Could not open state queue");
        goto err_state_queue;
    }

    CurrentStateQueue = xQueueCreate(1, sizeof(unsigned char));
    if (!CurrentStateQueue) {
        PRINT_ERROR("Could not open current state queue");
        goto err_current_state_queue;
    }

    InitTickQueue = xQueueCreate(1, sizeof(TickType_t));
    if (!InitTickQueue) {
        PRINT_ERROR("Could not open init tick queue");
        goto err_Init_Tick_queue;
    }

    //Timers
    

    

    //Infrastructure Tasks
    if (xTaskCreate(basicSequentialStateMachine, "StateMachine",
                    mainGENERIC_STACK_SIZE * 2, NULL,
                    configMAX_PRIORITIES - 1, &StateMachine) != pdPASS) {
        PRINT_TASK_ERROR("StateMachine");
        goto err_statemachine;
    }
    if (xTaskCreate(vSwapBuffers, "BufferSwapTask",
                    mainGENERIC_STACK_SIZE * 2, NULL, configMAX_PRIORITIES,
                    &BufferSwap) != pdPASS) {
        PRINT_TASK_ERROR("BufferSwapTask");
        goto err_bufferswap;
    }

    if (xTaskCreate(vGameLogic, "GameLogic",
                    mainGENERIC_STACK_SIZE * 2, NULL, mainGENERIC_PRIORITY,
                    &GameLogic) != pdPASS) {
        PRINT_TASK_ERROR("GameLogic");
        goto err_circle_manager;
    }

    //Normal Tasks
    if (xTaskCreate(vMenuDrawer, "MenuDrawer", mainGENERIC_STACK_SIZE * 2,
                    NULL, mainGENERIC_PRIORITY + 3, &MenuDrawer) != pdPASS) {
        PRINT_TASK_ERROR("MenuDrawer");
        goto err_MenuDrawer;
    }

    GameDrawer = xTaskCreateStatic(vGameDrawer, "Task2", STACK_SIZE,
                NULL, mainGENERIC_PRIORITY + 2, xStack, &GameDrawerBuffer);
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
    err_circle_manager:
        vTaskDelete(BufferSwap);
    err_bufferswap:
        vTaskDelete(StateMachine);
    err_statemachine:
        vQueueDelete(InitTickQueue);
    err_Init_Tick_queue:
        vQueueDelete(GameLogic);
    err_circle_queue:
        vQueueDelete(CurrentStateQueue);
    err_current_state_queue:
        vQueueDelete(StateQueue);
    err_state_queue:
        vSemaphoreDelete(ScreenLock);
    err_screen_lock:
        vSemaphoreDelete(DrawSignal);
    err_draw_signal:
        vSemaphoreDelete(buttons.lock);
    err_buttons_lock:
        tumSoundExit();
    err_init_audio:
        tumEventExit();
    err_init_events:
        tumDrawExit();
    err_init_drawing:
        safePrintExit();
    err_init_safe_print:
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

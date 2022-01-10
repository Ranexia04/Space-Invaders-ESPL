#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

#include "objects.h"



void vInsertCoin(void)
{
    xSemaphoreTake(my_player.lock, portMAX_DELAY);
    my_player.credits++;
    xSemaphoreGive(my_player.lock);
}

void vUseCoin(void)
{
    xSemaphoreTake(my_player.lock, portMAX_DELAY);
    my_player.credits--;
    xSemaphoreGive(my_player.lock);
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
    my_player.n_lives = INITIAL_LIVES;
    my_player.credits = 0;
    my_player.n_players = 1;
    my_player.lock = xSemaphoreCreateMutex();
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
    saved.n_lives = INITIAL_LIVES;
    saved.score = 0;
    saved.offset = 0;
    saved.credits = 0;
    saved.lock = xSemaphoreCreateMutex();
}

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

void vResetSpaceship(void)
{
    xSemaphoreTake(my_spaceship.lock, portMAX_DELAY);
    my_spaceship.x = SCREEN_WIDTH / 2 - my_spaceship.width / 2;
    xSemaphoreGive(my_spaceship.lock);
}

void vInitSpaceship(image_handle_t spaceship_image)
{
    my_spaceship.image = spaceship_image;
    my_spaceship.width = tumDrawGetLoadedImageWidth(my_spaceship.image);
    my_spaceship.height = tumDrawGetLoadedImageHeight(my_spaceship.image);
    my_spaceship.x = SCREEN_WIDTH / 2 - my_spaceship.width / 2;
    my_spaceship.y = SPACESHIP_Y;
    my_spaceship.lock = xSemaphoreCreateMutex();
}

void vPlayMonsterSound(void *args)
{
    tumSoundPlayUserSample("fastinvader1.wav");
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

void vInitMonsters(image_handle_t *monster_image, spritesheet_handle_t *monster_spritesheet)
{
    int i, j;

    for (i = 0; i < N_ROWS; i++) {
        for (j = 0; j < N_COLUMNS; j++) {
            if (i == 0) {
                my_monsters.monster[i][j].type = SMALL_MONSTER;
                my_monsters.monster[i][j].spritesheet = monster_spritesheet[0];
                my_monsters.monster[i][j].width = tumDrawGetLoadedImageWidth(monster_image[0]) / 2;
                my_monsters.monster[i][j].height = tumDrawGetLoadedImageHeight(monster_image[0]);
            }
            if (i == 1 || i == 2) {
                my_monsters.monster[i][j].type = MEDIUM_MONSTER;
                my_monsters.monster[i][j].spritesheet = monster_spritesheet[1];
                my_monsters.monster[i][j].width = tumDrawGetLoadedImageWidth(monster_image[1]) / 2;
                my_monsters.monster[i][j].height = tumDrawGetLoadedImageHeight(monster_image[1]);
            }
            if (i == 3 || i == 4) {
                my_monsters.monster[i][j].type = LARGE_MONSTER;
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

    my_monsters.callback = vPlayMonsterSound;
    my_monsters.args = NULL;
    my_monsters.lock = xSemaphoreCreateMutex();
}

void vResetMonsterDelay(void)
{
    TickType_t monster_delay;

    monster_delay = ORIGINAL_MONSTER_DELAY + saved.offset;
    xQueueOverwrite(MonsterDelayQueue, &monster_delay);
}

void vDecreaseMonsterDelay(void)
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

void vSetUpMothergunshipPVP(void)
{
    xSemaphoreTake(my_mothergunship.lock, portMAX_DELAY);
    my_mothergunship.x = SCREEN_WIDTH * 2 / 3 - my_mothergunship.width / 2;
    my_mothergunship.alive = 1;
    xSemaphoreGive(my_mothergunship.lock);
}

void vResetMothergunship(void)
{
    TickType_t timer_starting = xTaskGetTickCount();

    xSemaphoreTake(my_mothergunship.lock, portMAX_DELAY);
    my_mothergunship.direction = !my_mothergunship.direction;
    if (my_mothergunship.direction == LEFT_TO_RIGHT)
        my_mothergunship.x = -1 * my_mothergunship.width;
    if (my_mothergunship.direction == RIGHT_TO_LEFT)
        my_mothergunship.x = SCREEN_WIDTH;
    if (my_mothergunship.direction == STOP)
        my_mothergunship.direction = LEFT_TO_RIGHT;
    my_mothergunship.alive = 1;
    xSemaphoreGive(my_mothergunship.lock);
    xTimerChangePeriod(xMothergunshipTimer, ORIGINAL_TIMER, portMAX_DELAY);

    xQueueOverwrite(TimerStartingQueue, &timer_starting);
}

void vMothergunshipTimerCallback(TimerHandle_t xMothergunshipTimer)
{
    vResetMothergunship();
    tumSoundPlayUserSample("ufo_highpitch.wav");
}

void vKillMothergunship(void)
{
    xSemaphoreTake(my_mothergunship.lock, portMAX_DELAY);
    my_mothergunship.alive = 0;
    xSemaphoreGive(my_mothergunship.lock);
}

int vIsMothergunshipInBoundsLeft(void)
{
    if (my_mothergunship.x > 0)
        return 1;
    
    return 0;
}

int vIsMothergunshipInBoundsRight(void)
{
    if (my_mothergunship.x + my_mothergunship.width < SCREEN_WIDTH)
        return 1;
    
    return 0;
}

void vUpdateMothergunshipPositionPVP(void)
{
    if (my_mothergunship.alive) {
        xSemaphoreTake(my_mothergunship.lock, portMAX_DELAY);
        if (my_mothergunship.direction == LEFT_TO_RIGHT && vIsMothergunshipInBoundsRight())
            my_mothergunship.x = my_mothergunship.x + 1;
            
        if (my_mothergunship.direction == RIGHT_TO_LEFT && vIsMothergunshipInBoundsLeft())
            my_mothergunship.x = my_mothergunship.x - 1;
        xSemaphoreGive(my_mothergunship.lock);
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

void vInitMothergunship(image_handle_t mothergunship_image)
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

void vInitBunkers(image_handle_t *bunker_image)
{
    int i, j, k;

    for (k = 0; k < N_BUNKERS; k++) {
        for (i = 0; i < 2; i++) {
            for (j = 0; j < 3; j++) {
                if (i == 0 && j == 0) 
                    my_bunkers.bunker[k].component[i][j].image = bunker_image[0];
                
                if (i == 0 && j == 1) 
                    my_bunkers.bunker[k].component[i][j].image = bunker_image[1];
                
                if (i == 0 && j == 2) 
                    my_bunkers.bunker[k].component[i][j].image = bunker_image[2];
                
                if (i == 1 && j == 0) 
                    my_bunkers.bunker[k].component[i][j].image = bunker_image[3];
                
                if (i == 1 && j == 1) 
                    my_bunkers.bunker[k].component[i][j].image = bunker_image[4];
                
                if (i == 1 && j == 2) 
                    my_bunkers.bunker[k].component[i][j].image = bunker_image[5];
                
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
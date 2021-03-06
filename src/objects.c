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

void vUpdateAIScore(void)
{
    xSemaphoreTake(my_player.lock, portMAX_DELAY);
    my_player.score2 = my_player.score2 + 1000;
    xSemaphoreGive(my_player.lock);
}

void vPlayerGetHit(void)
{
    xSemaphoreTake(my_player.lock, portMAX_DELAY);
    my_player.n_lives--;
    xSemaphoreGive(my_player.lock);
}

void vUpdatePlayerScoreRandom(void)
{
    xSemaphoreTake(my_player.lock, portMAX_DELAY);
    my_player.score1 = my_player.score1 + 50 * (rand() % 4 + 1);
    xSemaphoreGive(my_player.lock);
}

void vUpdatePlayerScore(int i, int j)
{
    xSemaphoreTake(my_player.lock, portMAX_DELAY);
    if (my_monsters.monster[i][j].type == SMALL_MONSTER)
        my_player.score1 = my_player.score1 + 30;
    if (my_monsters.monster[i][j].type == MEDIUM_MONSTER)
        my_player.score1 = my_player.score1 + 20;
    if (my_monsters.monster[i][j].type == LARGE_MONSTER)
        my_player.score1 = my_player.score1 + 10;
    xSemaphoreGive(my_player.lock);
}

void vSetPlayerNumber(int n_players)
{
    xSemaphoreTake(my_player.lock, portMAX_DELAY);
    my_player.n_players = n_players;
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
    //the - 1 is there so that when we return to menu the credit is correct
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

void vDrawSpaceship(void)
{
    checkDraw(tumDrawLoadedImage(my_spaceship.image, my_spaceship.x,
                                my_spaceship.y),
                __FUNCTION__);
}

void vFixSpaceshipOutofBounds(void)
{
    if (my_spaceship.x < 0) {
        my_spaceship.x = 0;
    }
    if (my_spaceship.x + my_spaceship.width > SCREEN_WIDTH) {
        my_spaceship.x = SCREEN_WIDTH - my_spaceship.width;
    }
}

void vMoveSpaceship(int direction)
{
    if (xSemaphoreTake(my_spaceship.lock, 0) == pdTRUE) {
        my_spaceship.x = my_spaceship.x + direction * CHANGE_IN_POSITION;
        vFixSpaceshipOutofBounds();
        xSemaphoreGive(my_spaceship.lock);
    }
}

int vSpaceshipBulletActive(char *bullet_state)
{
    bullet_t my_bullet[MAX_OBJECTS];
    int bullet_active = 0, n_bullets, i = 0;

    strcpy(bullet_state, "PASSIVE");

    while(uxQueueMessagesWaiting(BulletQueue)) {
        xQueueReceive(BulletQueue, &my_bullet[i], portMAX_DELAY);
        if (my_bullet[i].type == SPACESHIP_BULLET) {
            bullet_active = 1;
            strcpy(bullet_state, "ATTACKING");
        }
        i++;
    }

    n_bullets = i;
    for (i = 0; i < n_bullets; i++) {
        xQueueSend(BulletQueue, &my_bullet[i], portMAX_DELAY);
    }

    return bullet_active;
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

#define BULLET_CHANGE 3

void vUpdateBulletPosition(void)
{
    int i = 0, n_bullets;
    bullet_t my_bullet[MAX_OBJECTS];

    while (uxQueueMessagesWaiting(BulletQueue)) {
        xQueueReceive(BulletQueue, &my_bullet[i], portMAX_DELAY);
        //spaceship bullets go up and other bullets go down
        if (my_bullet[i].type == SPACESHIP_BULLET)
            my_bullet[i].y = my_bullet[i].y - BULLET_CHANGE;
        if (my_bullet[i].type == MONSTER_BULLET || my_bullet[i].type == MOTHERSHIP_BULLET)
            my_bullet[i].y = my_bullet[i].y + BULLET_CHANGE;
        i++;
    }

    n_bullets = i;
    for (i = 0; i < n_bullets; i++) {
        xQueueSend(BulletQueue, &my_bullet[i], portMAX_DELAY);
    }
}

void vResetBulletQueue(void)
{
    bullet_t bullet;

    while (uxQueueMessagesWaiting(BulletQueue)) {
        xQueueReceive(BulletQueue, &bullet, portMAX_DELAY);
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
    if (my_bullet.type == SPACESHIP_BULLET)
        my_bullet.colour = Green;
    if (my_bullet.type == MONSTER_BULLET)
        my_bullet.colour = White;
    if (my_bullet.type == MOTHERSHIP_BULLET)
        my_bullet.colour = Red;

    xQueueSend(BulletQueue, &my_bullet, portMAX_DELAY);
}

void vDrawColision(colision_t my_colision)
{
    if (my_colision.image != NULL)
        checkDraw(tumDrawLoadedImage(my_colision.image, my_colision.x, my_colision.y), __FUNCTION__);
}

#define N_TICKS 20

void vDrawColisions(void)
{
    int i = 0, n_colisions;
    colision_t my_colision[MAX_OBJECTS];

    while(uxQueueMessagesWaiting(ColisionQueue)) {
        xQueueReceive(ColisionQueue, &my_colision[i], portMAX_DELAY);
        vDrawColision(my_colision[i]);
        my_colision[i].frame_number++;
        if (my_colision[i].frame_number > N_TICKS) {
            i--;
        }
        i++;
    }

    n_colisions = i;
    for (i = 0; i < n_colisions; i++) {
        xQueueSend(ColisionQueue, &my_colision[i], portMAX_DELAY);
    }
}

void vResetColisionQueue(void)
{
    colision_t colision;

    while (uxQueueMessagesWaiting(ColisionQueue)) {
        xQueueReceive(ColisionQueue, &colision, portMAX_DELAY);
    }
}

void createColision(int bullet_x, int bullet_y, image_handle_t image_buffer)
{
    colision_t my_colision;

    my_colision.image = image_buffer;
    //if we dont want a bullet colision to spawn an image
    // we can not give colision an image
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

void vDrawMonsters(void)
{
    int i, j;

    for (i = 0; i < N_ROWS; i++) {
        for (j = 0; j < N_COLUMNS; j++) {
            if (my_monsters.monster[i][j].alive)
                checkDraw(tumDrawSprite(my_monsters.monster[i][j].spritesheet,
                 my_monsters.monster[i][j].frametodraw, 0,
                 my_monsters.monster[i][j].x, my_monsters.monster[i][j].y),
                                         __FUNCTION__);
            
        }
    }
}

void vPlayMonsterSound(void *args)
{
    tumSoundPlayUserSample("fastinvader1.wav");
}

void vMonsterCallback(void)
{
    if (my_monsters.callback)
        my_monsters.callback(my_monsters.args);
}

void vKillMonster(int i, int j)
{
    xSemaphoreTake(my_monsters.lock, portMAX_DELAY);
    my_monsters.monster[i][j].alive = 0;
    xSemaphoreGive(my_monsters.lock);
}

int vComputeRightmostMonster(int i)
{
    int right_index = -1, j;

    for (j = 0; j < N_COLUMNS; j++) {
        if (my_monsters.monster[i][j].alive)
            right_index = j;
    }

    return right_index;
}

int vComputeLeftmostMonster(int i)
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
                if (my_monsters.monster[i][j].alive)
                    my_monsters.monster[i][j].y = my_monsters.monster[i][j].y + 10;
            }
        }
        xSemaphoreGive(my_monsters.lock);
    }
}

void vUpdateMonsterDirection(int *direction)
{
    int right_index, left_index, i;

    for (i = 0; i < N_ROWS; i++) {
        right_index = vComputeRightmostMonster(i);
        left_index = vComputeLeftmostMonster(i);
        if (right_index == -1 && left_index == -1)//when no monsters are alive
            continue;
        //if hit wall
        if (my_monsters.monster[i][left_index].x < 10 
                    || my_monsters.monster[i][right_index].x 
                    + my_monsters.monster[i][right_index].width > SCREEN_WIDTH - 10) {
            (*direction) = -1 * (*direction);
            vMonsterMoveCloser();
            return;
        }
    }
}

#define MONSTER_CHANGE 5

int vMoveMonster(int i, int j, int direction)
{
    if (xSemaphoreTake(my_monsters.lock, 0) == pdTRUE) {
        if (my_monsters.monster[i][j].alive) {
            my_monsters.monster[i][j].x = my_monsters.monster[i][j].x
                                             + MONSTER_CHANGE * direction;
            my_monsters.monster[i][j].frametodraw 
                    = !my_monsters.monster[i][j].frametodraw;
            xSemaphoreGive(my_monsters.lock);
            return 1;
        } else {
            xSemaphoreGive(my_monsters.lock);
            return 0;
        }
    }
    return 0;
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
            my_monsters.monster[i][j].frametodraw = 0;
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
            my_monsters.monster[i][j].frametodraw = 0;
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

void vDrawMothership(void)
{
    if (my_mothership.alive)
        checkDraw(tumDrawLoadedImage(my_mothership.image,
             my_mothership.x, my_mothership.y), __FUNCTION__);
}

void vSetUpMothershipPVP(void)
{
    xSemaphoreTake(my_mothership.lock, portMAX_DELAY);
    my_mothership.x = SCREEN_WIDTH * 2 / 3 - my_mothership.width / 2;
    my_mothership.alive = 1;
    xSemaphoreGive(my_mothership.lock);
}

void vResetMothership(void)
{
    xSemaphoreTake(my_mothership.lock, portMAX_DELAY);
    my_mothership.direction = -1 * my_mothership.direction;
    if (my_mothership.direction == LEFT_TO_RIGHT)
        my_mothership.x = 0;
    if (my_mothership.direction == RIGHT_TO_LEFT)
        my_mothership.x = SCREEN_WIDTH - my_mothership.width - 1;
    if (my_mothership.direction == STOP)
        my_mothership.direction = LEFT_TO_RIGHT;
    my_mothership.alive = 1;
    xSemaphoreGive(my_mothership.lock);

    TickType_t timer_starting = xTaskGetTickCount();

    if(my_player.n_players == 1) {
        xTimerChangePeriod(xMothershipTimer, ORIGINAL_TIMER, portMAX_DELAY);
        xQueueOverwrite(TimerStartingQueue, &timer_starting);
    }
}

void vMothershipTimerCallback(TimerHandle_t xMothershipTimer)
{
    vReviveMothership();
    tumSoundPlayUserSample("ufo_highpitch.wav");
}

void vKillMothership(void)
{
    xSemaphoreTake(my_mothership.lock, portMAX_DELAY);
    my_mothership.alive = 0;
    xSemaphoreGive(my_mothership.lock);
}

void vReviveMothership(void)
{
    xSemaphoreTake(my_mothership.lock, portMAX_DELAY);
    my_mothership.alive = 1;
    xSemaphoreGive(my_mothership.lock);
}

int vIsMothershipInBoundsLeft(void)
{
    if (my_mothership.x > 0)
        return 1;
    
    return 0;
}

int vIsMothershipInBoundsRight(void)
{
    if (my_mothership.x + my_mothership.width < SCREEN_WIDTH)
        return 1;
    
    return 0;
}

#define CHANGE_IN_POSITION_PVP 1

void vUpdateMothershipPositionPVP(void)
{
    if (my_mothership.alive) {
        xSemaphoreTake(my_mothership.lock, portMAX_DELAY);
        if (my_mothership.direction == LEFT_TO_RIGHT && vIsMothershipInBoundsRight())
            my_mothership.x = my_mothership.x + CHANGE_IN_POSITION_PVP;
            
        if (my_mothership.direction == RIGHT_TO_LEFT && vIsMothershipInBoundsLeft())
            my_mothership.x = my_mothership.x - CHANGE_IN_POSITION_PVP;
        xSemaphoreGive(my_mothership.lock);
    }
}

void vUpdateMothershipPosition(void)
{
    if (my_mothership.alive) {
        xSemaphoreTake(my_mothership.lock, portMAX_DELAY);
        my_mothership.x = my_mothership.x + 1 * my_mothership.direction;
        xSemaphoreGive(my_mothership.lock);
        if (!vIsMothershipInBoundsLeft() || !vIsMothershipInBoundsRight())
            goto reset_mothership;
    }

    return;

reset_mothership:
    vResetMothership();
    vKillMothership();
    return;
}

void vInitMothership(image_handle_t mothership_image)
{
    TickType_t timer_starting = xTaskGetTickCount();

    my_mothership.image = mothership_image;
    my_mothership.x = 0;
    my_mothership.y = MOTHERSHIP_Y;
    my_mothership.width = tumDrawGetLoadedImageWidth(my_mothership.image);
    my_mothership.height = tumDrawGetLoadedImageHeight(my_mothership.image);
    my_mothership.alive = 0;
    my_mothership.direction = LEFT_TO_RIGHT;
    my_mothership.lock = xSemaphoreCreateMutex();

    xQueueOverwrite(TimerStartingQueue, &timer_starting);
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

void vBunkerGetHit(int a, int i, int j)
{
    xSemaphoreTake(my_bunkers.lock, portMAX_DELAY);
    my_bunkers.bunker[a].component[i][j].damage++;
    xSemaphoreGive(my_bunkers.lock);
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

void vObjectSemaphoreDelete(void)
{
    if (!my_player.lock)
        vSemaphoreDelete(my_player.lock);
    if (!saved.lock)
        vSemaphoreDelete(saved.lock);
    if (!my_spaceship.lock)
        vSemaphoreDelete(my_spaceship.lock);
    if (!my_monsters.lock)
        vSemaphoreDelete(my_monsters.lock);
    if (!my_mothership.lock)
        vSemaphoreDelete(my_mothership.lock);
    if (!my_bunkers.lock)
        vSemaphoreDelete(my_bunkers.lock);
}
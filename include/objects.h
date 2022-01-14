#ifndef __OBJECTS__
#define __OBJECTS__

#define INITIAL_LIVES 3

#define ORIGINAL_TIMER 10000
#define ORIGINAL_MONSTER_DELAY 65

#define SPACESHIP_Y SCREEN_HEIGHT - 75
#define CHANGE_IN_POSITION 1

#define MAX_OBJECTS 10

#define BULLET_HEIGHT 8
#define SPACESHIP_BULLET 0
#define MONSTER_BULLET 1
#define MOTHERSHIP_BULLET 2

#define N_ROWS 5
#define N_COLUMNS 11

#define MONSTER_SPACING_V 34
#define MONSTER_SPACING_H 39

#define SMALL_MONSTER 0
#define MEDIUM_MONSTER 1
#define LARGE_MONSTER 2

#define N_BUNKERS 4

#define MOTHERSHIP_Y 83

#define LEFT_TO_RIGHT 1
#define RIGHT_TO_LEFT -1
#define STOP 0

typedef struct player {
    int score1;
    int highscore;
    int score2;
    int n_lives;
    int credits;
    int n_players;

    SemaphoreHandle_t lock;
} player_t;

typedef struct saved_values {
    int n_lives;
    int score;
    int offset;
    int credits;

    SemaphoreHandle_t lock;
} saved_values_t;

typedef struct spaceship {
    image_handle_t image;

    int x;
    int y;

    signed short width;
    signed short height;

    SemaphoreHandle_t lock;
} spaceship_t;

typedef struct bullet {
    int x;
    int y;

    signed short width;
    signed short height;

    unsigned int colour;

    int type;
} bullet_t;

typedef struct colision {
    image_handle_t image;

    int x;
    int y;

    signed short width;
    signed short height;

    int frame_number;
} colision_t;

typedef struct monster {
    spritesheet_handle_t spritesheet;

    int x;
    int y;

    signed short width;
    signed short height;

    int frametodraw;
    int type;
    int alive;
} monster_t;

typedef struct monster_grid {
    monster_t monster[N_ROWS][N_COLUMNS];

    callback_t callback; /**< monster callback */
    void *args; /**< monster callback args */

    SemaphoreHandle_t lock;
} monster_grid_t;

typedef struct mothership {
    image_handle_t image;

    int x;
    int y;

    signed short width;
    signed short height;

    int alive;
    int direction;

    SemaphoreHandle_t lock;
} mothership_t;

typedef struct bunker_component {
    image_handle_t image;

    int x;
    int y;

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

extern TimerHandle_t xMothershipTimer;

extern QueueHandle_t BulletQueue;
extern QueueHandle_t ColisionQueue;
extern QueueHandle_t MonsterDelayQueue;
extern QueueHandle_t TimerStartingQueue;

extern player_t my_player;

extern saved_values_t saved;

extern spaceship_t my_spaceship;

extern monster_grid_t my_monsters;

extern mothership_t my_mothership;

extern bunker_grid_t my_bunkers;

void vInsertCoin(void);

void vUseCoin(void);

void vUpdateAIScore(void);

void vPlayerGetHit(void);

void vUpdatePlayerScoreRandom(void);

void vUpdatePlayerScore(int i, int j);

void vSetPlayerNumber(int n_players);

void vResetPlayer(void);

void vInitPlayer(void);

void vUpdateSavedValues(void);

void vInitSavedValues(void);

void vMoveSpaceshipLeft(void);

void vMoveSpaceshipRight(void);

/**
 * @brief Receives all bullets from queue, and checks if any of them
 * are spaceship type
 * 
 * @param bullet_state This string should be "PASSIVE" if no bullet is active
 * and "ATTACKING" otherwise
 * @return Returns the same information as the string mentioned above but in integer form.
 */
int vSpaceshipBulletActive(char *bullet_state);

/**
 * @brief Resets position of the spaceship.
 * 
 */
void vResetSpaceship(void);

void vInitSpaceship(image_handle_t spaceship_image);

/**
 * @brief Receives every bullet in the queue, increments/decrements position 
 * and gives back to queue.
 * 
 */
void vUpdateBulletPosition(void);

/**
 * @brief Receives every bullet object in the queue, emptying it.
 * 
 */
void vResetBulletQueue(void);

/**
 * @brief Creates bullet object and sends it to queue
 * 
 * @param initial_x Initial x coordinate of bullet
 * @param initial_y Initial y coordinate of bullet
 * @param type Bullets can be shot from spaceship, monsters or mothergunship
 * Each bullet from each enemy can have different types and we use this to determine
 * if the bullet moves up or down, for example.
 */
void vShootBullet(int initial_x, int initial_y, int type);

/**
 * @brief Receives every colision object in the queue, emptying it.
 * 
 */
void vResetColisionQueue(void);

/**
 * @brief Creates a Colision object and sends it to queue by copy
 * 
 * @param bullet_x x coordinate of the bullet that created the colision
 * @param bullet_y y coordinate of the bullet that created the colision
 * @param image_buffer image to be drawn
 */
void createColision(int bullet_x, int bullet_y, image_handle_t image_buffer);

/**
 * @brief Plays monster moving sound
 * 
 * @param args Arguments of callback function.
 */
void vPlayMonsterSound(void *args);

void vMonsterCallback(void);

/**
 * @brief Kills indexed monster
 * 
 * @param i row of the monster
 * @param j column of the monster.
 */
void vKillMonster(int i, int j);

/**
 * @brief For a certain row, computes the leftmost alive monster
 * 
 * @param i row to compute left most alive monster
 * @return Returns the index of the leftmost alive monster, or -1 of all are dead.
 */
int vComputeRightmostMonster(int i);

/**
 * @brief For a certain row, computes the rightmost alive monster
 * 
 * @param i row to compute rightmost alive monster
 * @return Returns the index of the rightmost alive monster, or -1 of all are dead.
 */
int vComputeLeftmostMonster(int i);

/**
 * @brief Moves all the alive monster down a step
 * 
 */
void vMonsterMoveCloser(void);

/**
 * @brief Checks the direction that the monsters should be updated.
 * If any monster touches the walls reverts direction
 * 
 * 
 * @param direction Determines if the monster should be moved right or left.
 */
void vUpdateMonsterDirection(int *direction);

/**
 * @brief Moves selected monster left or right if alive
 * 
 * @param row row of the monster to move
 * @param column column of the monster to move
 * @param direction Determines if the monster should be moved right or left.
 * @return 1 if the monster was moved successfully and 0 otherwise
 */
int vMoveMonster(int row, int column, int direction);

/**
 * @brief Revives and resets position of all the monsters.
 * 
 */
void vResetMonsters(void);

void vInitMonsters(image_handle_t *monster_image, spritesheet_handle_t *monster_spritesheet);

void vResetMonsterDelay(void);

void vDecreaseMonsterDelay(void);

void vInitMonsterDelay(void);

void vSetUpMothershipPVP(void);

void vResetMothership(void);

void vMothershipTimerCallback(TimerHandle_t xMothershipTimer);

void vKillMothership(void);

int vIsMothershipInBoundsLeft(void);

int vIsMothershipInBoundsRight(void);

void vUpdateMothershipPositionPVP(void);

void vUpdateMothershipPosition(void);

void vInitMothership(image_handle_t mothership_image);

void vBunkerGetHit(int a, int i, int j);

void vResetBunkers(void);

void vInitBunkers(image_handle_t *bunker_image);

void vObjectSemaphoreDelete(void);

#endif
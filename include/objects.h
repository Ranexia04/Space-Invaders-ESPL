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

/**
 * @brief Holds information regarding the player.
 * 
 */
typedef struct player {
    int score1;
    int highscore;
    int score2;
    int n_lives;
    int credits;
    int n_players;

    SemaphoreHandle_t lock;
} player_t;

/**
 * @brief Object with some values to be saved
 * so that when we can reset values to those
 * different that the original values.
 * 
 */
typedef struct saved_values {
    int n_lives;
    int score;
    int offset;
    int credits;

    SemaphoreHandle_t lock;
} saved_values_t;

/**
 * @brief Hold spaceship information.
 * 
 */
typedef struct spaceship {
    image_handle_t image;

    int x;
    int y;

    signed short width;
    signed short height;

    SemaphoreHandle_t lock;
} spaceship_t;

/**
 * @brief Holds bullet information.
 * 
 */
typedef struct bullet {
    int x;
    int y;

    signed short width;
    signed short height;

    unsigned int colour;

    int type;
} bullet_t;

/**
 * @brief Holds colision information.
 * 
 */
typedef struct colision {
    image_handle_t image;

    int x;
    int y;

    signed short width;
    signed short height;

    int frame_number;
} colision_t;

/**
 * @brief Holds monster information.
 * 
 */
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

/**
 * @brief Composes a grid of monster objects
 * to be handled safely with semaphore.
 * 
 */
typedef struct monster_grid {
    monster_t monster[N_ROWS][N_COLUMNS];

    callback_t callback; /**< monster callback */
    void *args; /**< monster callback args */

    SemaphoreHandle_t lock;
} monster_grid_t;

/**
 * @brief Holds mothership information.
 * 
 */
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

/**
 * @brief Holds information for each piece of
 * each bunker.
 * 
 */
typedef struct bunker_component {
    image_handle_t image;

    int x;
    int y;

    signed short width;
    signed short height;

    int damage;
} bunker_component_t;

/**
 * @brief Each bunker is to be made out of
 * pieces to be handled separatly.
 * 
 */
typedef struct bunker {
    bunker_component_t component[2][3];
} bunker_t;

/**
 * @brief Composes a grid of bunker objects
 * to be handled safely with semaphore.
 * 
 */
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

extern void checkDraw(unsigned char status, const char *msg);

/**
 * @brief Increments credit variable.
 * 
 */
void vInsertCoin(void);

/**
 * @brief Decrements credit variable.
 * 
 */
void vUseCoin(void);

/**
 * @brief Updates player 2 score when mothership kills spaceship.
 * 
 */
void vUpdateAIScore(void);

/**
 * @brief Decrement number of lives.
 * 
 */
void vPlayerGetHit(void);

/**
 * @brief Updates player score using some randomness.
 * 
 */
void vUpdatePlayerScoreRandom(void);

/**
 * @brief Updates player 1 score based on monster killed
 * 
 * @param i row of the monster killed
 * @param j column of the monster killed.
 */
void vUpdatePlayerScore(int i, int j);

/**
 * @brief Modifies number of player variable
 * 
 * @param n_players Number of players to be set.
 */
void vSetPlayerNumber(int n_players);

/**
 * @brief Resets player object to either original values
 * or values saved on "saved" object.
 * 
 */
void vResetPlayer(void);

/**
 * @brief Initiates player object.
 * 
 */
void vInitPlayer(void);

/**
 * @brief Update values to be saved
 * based on the current ones. Credit variable
 * is saved but decremented so that the coin
 * is effectively used.
 * 
 */
void vUpdateSavedValues(void);

/**
 * @brief Initiates saved object.
 * 
 */
void vInitSavedValues(void);

/**
 * @brief Draws spaceship.
 * 
 */
void vDrawSpaceship(void);

/**
 * @brief Prevents the spaceship from moving out of the screen.
 * 
 */
void vFixSpaceshipOutofBounds(void);

/**
 * @brief Moves spaceship right or left
 * 
 * @param direction If direction is 1 then moves right, if -1 then moves left
 */
void vMoveSpaceship(int direction);

/**
 * @brief Receives all bullets from queue, and checks 
 * if any of them are spaceship type
 * 
 * @param bullet_state This string should be "PASSIVE" 
 * if no bullet is active and "ATTACKING" otherwise
 * @return Returns the same information as the string 
 * mentioned above but in integer form.
 */
int vSpaceshipBulletActive(char *bullet_state);

/**
 * @brief Resets position of the spaceship.
 * 
 */
void vResetSpaceship(void);

/**
 * @brief Initiates spaceship object.
 * 
 * @param spaceship_image Image of spaceship.
 */
void vInitSpaceship(image_handle_t spaceship_image);

/**
 * @brief Draws bullets.
 * 
 */
void vDrawBullets(void);

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
 * @brief Draws colision.
 * 
 * @param my_colision 
 */
void vDrawColision(colision_t my_colision);

/**
 * @brief Receives colision object from queue, draws the image,
 *  increments count and returns colision to queue.
 * If the colision has been drawn for more than N_TICKS 
 *  does not return it to queue, deleting it 
 * 
 */
void vDrawColisions(void);

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
 * @param image_buffer Image of colision to be drawn.
 */
void createColision(int bullet_x, int bullet_y, image_handle_t image_buffer);

/**
 * @brief Draws all alive monsters.
 * 
 */
void vDrawMonsters(void);

/**
 * @brief Plays monster moving sound
 * 
 * @param args Arguments of callback function.
 */
void vPlayMonsterSound(void *args);

/**
 * @brief Callback of monster object.
 * 
 */
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
 * @brief Resets position of all the monsters
 * and sets alive variable to 1.
 * 
 */
void vResetMonsters(void);

/**
 * @brief Initiates monster grid
 * 
 * @param monster_image Image of each monster and is used to 
 * compute monster width and height.
 * @param monster_spritesheet Spritesheet of monster frames 
 * to be drawn.
 */
void vInitMonsters(image_handle_t *monster_image, spritesheet_handle_t *monster_spritesheet);

/**
 * @brief Overwrites the queue with the original monster delay
 * plus a certain offset that is saved on "saved" object.
 * 
 */
void vResetMonsterDelay(void);

/**
 * @brief Decrements current monster delay 
 * overwrites queue.
 * 
 */
void vDecreaseMonsterDelay(void);

/**
 * @brief Overwrites a defined value to the
 * monster delay queue.
 * 
 */
void vInitMonsterDelay(void);

/**
 * @brief Draws mothership.
 * 
 */
void vDrawMothership(void);

/**
 * @brief Sets initial position of mothership
 * and sets alive variable to 1.
 * 
 */
void vSetUpMothershipPVP(void);

/**
 * @brief Revives mothergunship, reverses direction and updates timer.
 * 
 */
void vResetMothership(void);

/**
 * @brief This function gets called when the mothership timer expires.
 * 
 * @param xMothershipTimer 
 */
void vMothershipTimerCallback(TimerHandle_t xMothershipTimer);

/**
 * @brief Set .alive variable to 0 safely.
 * 
 */
void vKillMothership(void);

/**
 * @brief Set .alive variable to 1 safely.
 * 
 */
void vReviveMothership(void);

/**
 * @brief Checks if mothership is out of left of screen.
 * 
 * @return Return 1 if out of left of screen, and 0 otherwise.
 */
int vIsMothershipInBoundsLeft(void);

/**
 * @brief Checks if mothership is out of right of screen.
 * 
 * @return Return 1 if out of right of screen, and 0 otherwise.
 */
int vIsMothershipInBoundsRight(void);

/**
 * @brief Increments or decrements mothership position depeding
 * on direction variable. Does not change position if out of bounds
 * or if mothership is dead.
 * 
 */
void vUpdateMothershipPositionPVP(void);

/**
 * @brief Increments or decrements mothership position depeding
 * on direction variable. Does not change position if mothership
 * is dead. If mothership goes out of bounds resets the mothership
 * and the associated timer.
 * 
 */
void vUpdateMothershipPosition(void);

/**
 * @brief Initiates mothership object.
 * 
 * @param mothership_image Image of mothership.
 */
void vInitMothership(image_handle_t mothership_image);

/**
 * @brief Draws not too damaged bunker components.
 * 
 */
void vDrawBunkers(void);

/**
 * @brief Increments damage variable of bunker piece
 * 
 * @param a Number of bunker that got hit
 * @param i Row of bunker piece that got hit
 * @param j Column of bunker piece that got hit.
 */
void vBunkerGetHit(int a, int i, int j);

/**
 * @brief Resets damage on all bunkers and bunker pieces.
 * 
 */
void vResetBunkers(void);

/**
 * @brief Initiates bunker object.
 * 
 * @param bunker_image Array containing the images of each
 * part of each bunker.
 */
void vInitBunkers(image_handle_t *bunker_image);

/**
 * @brief Deletes all semaphores created.
 * 
 */
void vObjectSemaphoreDelete(void);

#endif
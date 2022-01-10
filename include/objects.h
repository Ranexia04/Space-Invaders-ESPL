#ifndef __OBJECTS__
#define __OBJECTS__

#define INITIAL_LIVES 3

#define ORIGINAL_TIMER 10000
#define ORIGINAL_MONSTER_DELAY 65

#define SPACESHIP_Y SCREEN_HEIGHT - 75
#define CHANGE_IN_POSITION 1

#define BULLET_HEIGHT 8
#define SPACESHIP_BULLET 0
#define MONSTER_BULLET 1
#define MOTHERGUNSHIP_BULLET 2

#define N_ROWS 5
#define N_COLUMNS 11

#define MONSTER_SPACING_V 34
#define MONSTER_SPACING_H 39

#define SMALL_MONSTER 0
#define MEDIUM_MONSTER 1
#define LARGE_MONSTER 2

#define N_BUNKERS 4

#define MOTHERGUNSHIP_Y 83

#define LEFT_TO_RIGHT 0
#define RIGHT_TO_LEFT 1
#define STOP 2

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

    int todraw;
    int type;
    int alive;
} monster_t;

typedef struct monster_grid {
    monster_t monster[N_ROWS][N_COLUMNS];

    callback_t callback; /**< monster callback */
    void *args; /**< monster callback args */

    SemaphoreHandle_t lock;
} monster_grid_t;

typedef struct mothergunship {
    image_handle_t image;

    int x;
    int y;

    signed short width;
    signed short height;

    int alive;
    int direction;

    SemaphoreHandle_t lock;
} mothergunship_t;

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

extern TimerHandle_t xMothergunshipTimer;

extern QueueHandle_t MonsterDelayQueue;
extern QueueHandle_t TimerStartingQueue;

extern player_t my_player;

extern saved_values_t saved;

extern spaceship_t my_spaceship;

extern monster_grid_t my_monsters;

extern mothergunship_t my_mothergunship;

extern bunker_grid_t my_bunkers;

void vInsertCoin(void);

void vUseCoin(void);

void vResetPlayer(void);

void vInitPlayer(void);

void vUpdateSavedValues(void);

void vInitSavedValues(void);

void vMoveSpaceshipLeft(void);

void vMoveSpaceshipRight(void);

void vResetSpaceship(void);

void vInitSpaceship(image_handle_t spaceship_image);

void vPlayMonsterSound(void *args);

void vResetMonsters(void);

void vInitMonsters(image_handle_t *monster_image, spritesheet_handle_t *monster_spritesheet);

void vResetMonsterDelay(void);

void vDecreaseMonsterDelay(void);

void vInitMonsterDelay(void);

void vSetUpMothergunshipPVP(void);

void vResetMothergunship(void);

void vMothergunshipTimerCallback(TimerHandle_t xMothergunshipTimer);

void vKillMothergunship(void);

int vIsMothergunshipInBoundsLeft(void);

int vIsMothergunshipInBoundsRight(void);

void vUpdateMothergunshipPositionPVP(void);

void vUpdateMothergunshipPosition(void);

void vInitMothergunship(image_handle_t mothergunship_image);

void vResetBunkers(void);

void vInitBunkers(image_handle_t *bunker_image);

#endif
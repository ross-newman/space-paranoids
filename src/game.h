#ifndef GAME_H
#define GAME_H

#include <SDL.h>
#include <vector>
#include <string>
#include "coriolis/input.h"

namespace Coralis {
    class Engine;
    class Input;
}

struct Vector2 {
    float x = 0.0f;
    float y = 0.0f;
};

enum class EnemyType {
    Basic,
    Advanced,
    Boss
};

struct Entity {
    Vector2 position;
    EnemyType type;
    float health;
    Vector2 velocity;
    std::string asset_id;
};

struct Bullet {
    Vector2 position;
    Vector2 velocity;
    float damage;
    float lifetime;
};

class GameState {
public:
    GameState();
    ~GameState();

    void loadAssets(Coralis::Engine& engine);
    void handleInput(Coralis::Input& input, float delta_time);
    void updatePlayer(float delta_time);
    void updateEnemies(float delta_time);
    void updateBullets(float delta_time);
    void checkCollisions();

private:
    std::vector<Entity> enemies;
    std::vector<Bullet> player_bullets;
    Vector2 player_position;
};

#endif // GAME_H
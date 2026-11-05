#ifndef GAME_H
#define GAME_H

#include <string>
#include <vector>
#include <memory>
#include "coralis/Engine.h" // Assuming Coralis provides a core Engine class

// --- Game Constants & Enums ---
enum class EnemyType {
    RECOGNIZER,
    TANK,
    TURRET
};

// --- Structs ---

struct Vector2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct Entity {
    Vector2 position;
    Vector2 velocity;
    float health;
    EnemyType type;
    std::string asset_id; // e.g., "recognizer_sprite"
};

struct Bullet {
    Vector2 position;
    Vector2 direction;
    float damage;
    float lifetime;
};

// --- Game State ---
class GameState {
public:
    // Player State
    Vector2 player_position = {0.0f, 0.0f};
    float player_health = 100.0f;
    int ammo_count = 3; // Recognizer shots
    int score = 0;

    // Game Entities
    std::vector<Entity> enemies;
    std::vector<Bullet> player_bullets;
    std::vector<Bullet> enemy_bullets;

    // Game Parameters
    float game_time = 0.0f;
    bool is_running = false;

    // Asset Management (Simplified)
    void loadAssets(Coralis::Engine& engine);

    // Core Logic Functions
    void spawnEnemy(EnemyType type, Vector2 position);
    void updatePlayer(float delta_time);
    void updateEnemies(float delta_time);
    void updateBullets(float delta_time);
    void checkCollisions();
    void handleInput(const Coralis::Input& input, float delta_time);
};

#endif // GAME_H
#include "game.h"
#include <cmath>
#include <cstdlib>
#include <algorithm>

GameState::GameState() {
    player_position = {0.0f, 0.0f};
}

GameState::~GameState() {}

void GameState::loadAssets(Coralis::Engine& engine) {}

void GameState::spawnEnemy(EnemyType type, Vector2 position) {
    Entity new_enemy;
    new_enemy.position = position;
    new_enemy.type = type;
    new_enemy.health = 100.0f;
    new_enemy.velocity = { (float)(rand() % 10 - 5), (float)(rand() % 10 - 5) };
    new_enemy.asset_id = "enemy_sprite_" + std::to_string(static_cast<int>(type));
    enemies.push_back(new_enemy);
}

void GameState::updatePlayer(float delta_time) {}

void GameState::updateEnemies(float delta_time) {
    for (auto& enemy : enemies) {
        enemy.position.x += enemy.velocity.x * delta_time;
        enemy.position.y += enemy.velocity.y * delta_time;
    }
}

void GameState::updateBullets(float delta_time) {
    player_bullets.erase(
        std::remove_if(player_bullets.begin(), player_bullets.end(), 
                       [&](const Bullet& b) { return b.lifetime <= 0.0f; }),
        player_bullets.end()
    );
}

void GameState::checkCollisions() {}

void GameState::handleInput(Coralis::Input& input, float delta_time) {
    if (input.is_key_pressed(SDL_SCANCODE_SPACE)) {
        player_bullets.push_back({
            player_position, 
            {0.0f, 1.0f},
            10.0f, 
            2.0f
        });
    }
}
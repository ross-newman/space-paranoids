#include <iostream>
#include <memory>
#include <SDL2/SDL.h> // Include SDL header
#include "game.h"
// Removed the problematic include for Engine.h

// Function prototypes for initialization and running the game
void initializeGame(Coralis::Engine* engine, GameState& state);
void runGameLoop(Coralis::Engine* engine, GameState& state);

int main() {
    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL could not initialize! SDL_Error: " << SDL_GetError() << std::endl;
        return 1;
    }

    // 1. Initialize the Coralis Engine (assuming it handles window/renderer setup)
    Coralis::Engine* engine = nullptr;
    // We must assume the engine object is created and managed elsewhere, 
    // or we must use a raw pointer if the full definition is unavailable here.
    // For now, we initialize it to nullptr and rely on the initialization function 
    // or the engine library to manage its lifecycle.
    
    // 2. Initialize the game state and load assets
    GameState game_state;
    initializeGame(engine, game_state);

    std::cout << "--- Space Paranoids: Tron Grid Game Initialized ---" << std::endl;
    std::cout << "Running game loop... (Press Ctrl+C to exit)" << std::endl;

    // 3. Run the main game loop
    runGameLoop(engine, game_state);

    std::cout << "Game loop finished." << std::endl;
    
    // Cleanup SDL
    SDL_Quit();
    return 0;
}
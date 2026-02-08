#include "attic.h"
#include <iostream>

Attic::Attic() : atticTexture(nullptr) {
}

Attic::~Attic() {
    cleanup();
}

bool Attic::init(SDL_Renderer* renderer) {
    // Load attic texture
    SDL_Surface* surface = IMG_Load("../assets/attic.png");
    if (!surface) {
        std::cout << "Failed to load attic image! SDL_image Error: " << IMG_GetError() << std::endl;
        return false;
    }
    
    atticTexture = SDL_CreateTextureFromSurface(renderer, surface);
    if (!atticTexture) {
        std::cout << "Failed to create attic texture! SDL Error: " << SDL_GetError() << std::endl;
        SDL_FreeSurface(surface);
        return false;
    }
    
    // Set the attic position and size to match screen dimensions
    atticRect = {0, 0, ATTIC_SCREEN_WIDTH, ATTIC_SCREEN_HEIGHT};
    
    SDL_FreeSurface(surface);
    return true;
}

void Attic::render(SDL_Renderer* renderer) {
    if (atticTexture) {
        SDL_RenderCopy(renderer, atticTexture, NULL, &atticRect);
    }
}

void Attic::cleanup() {
    if (atticTexture) {
        SDL_DestroyTexture(atticTexture);
        atticTexture = nullptr;
    }
}
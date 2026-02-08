#pragma once

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <string>
#include <vector>
#include <fstream>
#include <iostream>
#include <codecvt>
#include <locale>

// Screen dimensions
const int SCREEN_WIDTH = 1024;
const int SCREEN_HEIGHT = 768;

struct BookSprite {
    SDL_Rect rect;
    SDL_Texture* texture;
    float scale;
    std::string contentPath;  
    bool isHovered;  // Track hover state
};

// Book content variables
extern std::vector<std::string> currentBookWords;
extern size_t currentWordIndex;      // Current word being animated
extern size_t lastDisplayedWord;     // Last word displayed in the book
extern Uint32 lastWordTime;
extern bool showNextArrow;
extern SDL_Rect nextArrowRect;
extern bool isHoveringNextArrow;

// Add state for book page view
extern bool showingBookPage;
extern SDL_Texture* bookPageTexture;
extern SDL_Rect libraryBackButtonRect;
extern SDL_Rect bookPageBackButtonRect;
extern std::vector<BookSprite> bookSprites;

// Helper function to load texture
inline SDL_Texture* loadTexture(const char* path, SDL_Renderer* renderer) {
    SDL_Surface* surface = IMG_Load(path);
    if (!surface) {
        printf("Failed to load image %s! SDL_image Error: %s\n", path, IMG_GetError());
        return nullptr;
    }
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FreeSurface(surface);
    return texture;
}

void initBackButtons();
void renderBookSprites(SDL_Renderer* renderer);
void handleBookClick(int mouseX, int mouseY);
bool isMouseOverLibraryBack(int mouseX, int mouseY);
bool isMouseOverBookPageBack(int mouseX, int mouseY);
bool isMouseOverNextArrow(int mouseX, int mouseY);
void renderBookPage(SDL_Renderer* renderer);
void cleanupLibraryTextures();
void loadBookContent(const std::string& contentPath);
void updateTextAnimation();
void handleNextArrowClick();
#ifndef ATTIC_H
#define ATTIC_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <string>

// Use same screen dimensions as main game
const int ATTIC_SCREEN_WIDTH = 1024;
const int ATTIC_SCREEN_HEIGHT = 768;

class Attic {
public:
    Attic();
    ~Attic();
    
    bool init(SDL_Renderer* renderer);
    void render(SDL_Renderer* renderer);
    void cleanup();
    
private:
    SDL_Texture* atticTexture;
    SDL_Rect atticRect;
};

#endif // ATTIC_H
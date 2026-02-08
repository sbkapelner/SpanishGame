#ifndef KITCHEN_H
#define KITCHEN_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <string>

// Use same screen dimensions as main game
const int KITCHEN_SCREEN_WIDTH = 1024;
const int KITCHEN_SCREEN_HEIGHT = 768;

class Kitchen {
public:
    Kitchen();
    ~Kitchen();
    
    bool init(SDL_Renderer* renderer);
    void render(SDL_Renderer* renderer);
    void cleanup();
    bool isInZoomClickArea(int x, int y) const;
    bool isInClockHoverArea(int x, int y) const;
    bool isZoomed() const { return showingZoomedView; }
    void toggleZoom() { showingZoomedView = !showingZoomedView; }
    void updateClockHands();
    void setClockHandTextures(SDL_Texture* hourHand, SDL_Texture* minuteHand);
    
private:
    SDL_Texture* kitchenTexture;
    SDL_Texture* kitchenZoomTexture;
    SDL_Texture* clockHourHandTexture;
    SDL_Texture* clockMinuteHandTexture;
    SDL_Rect kitchenRect;
    bool showingZoomedView;
    
    // Clock hand properties
    float hourHandRotation;
    float minuteHandRotation;
    Uint32 lastHandRotationTime;
    
    void renderClockHands(SDL_Renderer* renderer);
};

#endif // KITCHEN_H

#include "kitchen.h"
#include <iostream>
#include <cmath>

Kitchen::Kitchen() : 
    kitchenTexture(nullptr), 
    kitchenZoomTexture(nullptr), 
    clockHourHandTexture(nullptr),
    clockMinuteHandTexture(nullptr),
    showingZoomedView(false),
    hourHandRotation(0.0f),
    minuteHandRotation(0.0f),
    lastHandRotationTime(0),
    selectedHour(12),
    selectedMinute(0) {
}

Kitchen::~Kitchen() {
    cleanup();
}

bool Kitchen::init(SDL_Renderer* renderer) {
    // Load kitchen texture
    SDL_Surface* surface = IMG_Load("../assets/kitchen.png");
    if (!surface) {
        std::cout << "Failed to load kitchen image! SDL_image Error: " << IMG_GetError() << std::endl;
        return false;
    }
    
    kitchenTexture = SDL_CreateTextureFromSurface(renderer, surface);
    if (!kitchenTexture) {
        std::cout << "Failed to create kitchen texture! SDL Error: " << SDL_GetError() << std::endl;
        SDL_FreeSurface(surface);
        return false;
    }
    SDL_FreeSurface(surface);

    // Load kitchen zoom texture
    surface = IMG_Load("../assets/clock_zoom.png");
    if (!surface) {
        std::cout << "Failed to load kitchen zoom image! SDL_image Error: " << IMG_GetError() << std::endl;
        return false;
    }
    
    kitchenZoomTexture = SDL_CreateTextureFromSurface(renderer, surface);
    if (!kitchenZoomTexture) {
        std::cout << "Failed to create kitchen zoom texture! SDL Error: " << SDL_GetError() << std::endl;
        SDL_FreeSurface(surface);
        return false;
    }
    
    // Set the kitchen position and size to match screen dimensions
    kitchenRect = {0, 0, KITCHEN_SCREEN_WIDTH, KITCHEN_SCREEN_HEIGHT};
    
    SDL_FreeSurface(surface);
    return true;
}

void Kitchen::render(SDL_Renderer* renderer) {
    if (showingZoomedView) {
        if (kitchenZoomTexture) {
            SDL_RenderCopy(renderer, kitchenZoomTexture, NULL, &kitchenRect);
            renderClockHands(renderer);
        }
    } else {
        if (kitchenTexture) {
            SDL_RenderCopy(renderer, kitchenTexture, NULL, &kitchenRect);
        }
    }
}

void Kitchen::cleanup() {
    if (kitchenTexture) {
        SDL_DestroyTexture(kitchenTexture);
        kitchenTexture = nullptr;
    }
    if (kitchenZoomTexture) {
        SDL_DestroyTexture(kitchenZoomTexture);
        kitchenZoomTexture = nullptr;
    }
    // Don't destroy clock hand textures - they're managed by main.cpp
    clockHourHandTexture = nullptr;
    clockMinuteHandTexture = nullptr;
}

void Kitchen::setClockHandTextures(SDL_Texture* hourHand, SDL_Texture* minuteHand) {
    clockHourHandTexture = hourHand;
    clockMinuteHandTexture = minuteHand;
}

void Kitchen::updateClockHands() {
    lastHandRotationTime = SDL_GetTicks();
}

void Kitchen::setDisplayedTime(int hour, int minute) {
    if (hour < 1 || hour > 12) {
        hour = 12;
    }
    if (minute < 0 || minute > 59) {
        minute = 0;
    }

    selectedHour = hour;
    selectedMinute = minute;

    minuteHandRotation = minute * 6.0f;
    hourHandRotation = ((hour % 12) * 30.0f) + (minute * 0.5f);
}

void Kitchen::resetDisplayedTime() {
    setDisplayedTime(12, 0);
}

bool Kitchen::matchesTime(int hour, int minute) const {
    return selectedHour == hour && selectedMinute == minute;
}

bool Kitchen::handleClockClick(int x, int y) {
    if (!showingZoomedView || !isInClockFace(x, y)) {
        return false;
    }

    const int clockCenterX = 503;
    const int clockCenterY = 333;
    const int innerRadiusThreshold = 135;
    const float twoPi = 6.28318530717958647692f;
    const float startAngle = -1.57079632679f;

    const int dx = x - clockCenterX;
    const int dy = y - clockCenterY;
    const float radius = std::sqrt(static_cast<float>((dx * dx) + (dy * dy)));
    float angle = std::atan2(static_cast<float>(dy), static_cast<float>(dx)) - startAngle;

    while (angle < 0.0f) {
        angle += twoPi;
    }
    while (angle >= twoPi) {
        angle -= twoPi;
    }

    if (radius >= innerRadiusThreshold) {
        int minute = static_cast<int>(std::lround((angle / twoPi) * 60.0f)) % 60;
        setDisplayedTime(selectedHour, minute);
    } else {
        int hourIndex = static_cast<int>(std::lround((angle / twoPi) * 12.0f)) % 12;
        int hour = (hourIndex == 0) ? 12 : hourIndex;
        setDisplayedTime(hour, selectedMinute);
    }

    return true;
}

void Kitchen::renderClockHands(SDL_Renderer* renderer) {
    if (!showingZoomedView || !clockHourHandTexture || !clockMinuteHandTexture) return;

    const int clockCenterX = 503;
    const int clockCenterY = 333;

    // Hour hand texture setup: short hand
    const int hourHandWidth = (int)(60 * 0.70f);
    const int hourHandHeight = (int)(256 * 0.70f);
    const int hourPivotX = 24;
    const int hourPivotY = 149;

    // Minute hand texture setup: long hand
    const int minuteHandWidth = (int)(142 * 0.20f);
    const int minuteHandHeight = (int)(504 * 0.50f);
    const int minutePivotX = (int)(56 * 0.40f);
    const int minutePivotY = 320;

    SDL_Rect minuteHandRect;
    minuteHandRect.w = minuteHandWidth;
    minuteHandRect.h = minuteHandHeight;
    minuteHandRect.x = clockCenterX - (int)(minutePivotX * 0.50f);
    minuteHandRect.y = clockCenterY - (int)(minutePivotY * 0.50f);

    SDL_Rect hourHandRect;
    hourHandRect.w = hourHandWidth;
    hourHandRect.h = hourHandHeight;
    hourHandRect.x = clockCenterX - (int)(hourPivotX * 0.70f);
    hourHandRect.y = clockCenterY - (int)(hourPivotY * 0.70f);

    SDL_Point minuteCenter = {
        (int)(minutePivotX * 0.50f),
        (int)(minutePivotY * 0.50f)
    };

    SDL_Point hourCenter = {
        (int)(hourPivotX * 0.70f),
        (int)(hourPivotY * 0.70f)
    };

    // Render order preserved from latest tuning: hour first, then minute on top
    SDL_RenderCopyEx(renderer, clockHourHandTexture, NULL, &hourHandRect, hourHandRotation, &hourCenter, SDL_FLIP_NONE);
    SDL_RenderCopyEx(renderer, clockMinuteHandTexture, NULL, &minuteHandRect, minuteHandRotation, &minuteCenter, SDL_FLIP_NONE);

    // Debug center markers
    const int hourCenterX = hourHandRect.x + hourCenter.x;
    const int hourCenterY = hourHandRect.y + hourCenter.y;
    const int minuteCenterX = minuteHandRect.x + minuteCenter.x;
    const int minuteCenterY = minuteHandRect.y + minuteCenter.y;

    auto drawDot = [&](int x, int y, Uint8 r, Uint8 g, Uint8 b) {
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, r, g, b, 255);
        SDL_Rect dot = {x - 3, y - 3, 7, 7};
        SDL_RenderFillRect(renderer, &dot);
    };

    drawDot(minuteCenterX, minuteCenterY, 255, 0, 0); // Red: minute hand center
    drawDot(hourCenterX, hourCenterY, 0, 255, 0);     // Green: hour hand center
    drawDot(clockCenterX, clockCenterY, 0, 0, 255);   // Blue: clock zoom center

    // Debug: 12 radial divisions through the clock center (red)
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
    const float twoPi = 6.28318530717958647692f;
    const float startAngle = -1.57079632679f; // 12 o'clock
    const int lineLen = 1000; // long enough to reach screen edge from center

    // Store adjusted hour-line angles so we can subdivide each segment into fifths
    float hourAngles[12];

    for (int i = 0; i < 12; ++i) {
        float a = startAngle + (twoPi * i / 12.0f);

        // Nudge 1,2,7,8 o'clock lines slightly counter-clockwise
        if (i == 1 || i == 2 || i == 7 || i == 8) {
            a -= 0.035f; // ~2 degrees CCW
        }

        // Nudge 3,4,5,6,10,11 o'clock lines slightly clockwise
        if (i == 3 || i == 4 || i == 5 || i == 6 || i == 10 || i == 11) {
            a += 0.035f; // ~2 degrees CW
        }

        // Extra clockwise tweak for 4 and 5 o'clock
        if (i == 4 || i == 5) {
            a += 0.030f; // additional CW boost
        }

        // Tiny correction: 4,5 slightly clockwise
        if (i == 4 || i == 5) {
            a += 0.008f;
        }

        // Tiny correction: 6,7,8 slightly counter-clockwise
        if (i == 6 || i == 7 || i == 8) {
            a -= 0.008f;
        }

        // Extra tiny correction: 10 and 11 slightly clockwise
        if (i == 10 || i == 11) {
            a += 0.004f;
        }

        hourAngles[i] = a;

        int dx = (int)std::lround(std::cos(a) * lineLen);
        int dy = (int)std::lround(std::sin(a) * lineLen);

        // Single ray from center outward (no duplicate opposite line)
        SDL_RenderDrawLine(renderer,
            clockCenterX, clockCenterY,
            clockCenterX + dx, clockCenterY + dy);
    }

    // Subdivide each 1/12 segment into fifths with green lines (4 inner marks each)
    SDL_SetRenderDrawColor(renderer, 0, 255, 0, 220);
    for (int i = 0; i < 12; ++i) {
        float a0 = hourAngles[i];
        float a1 = hourAngles[(i + 1) % 12];
        if (a1 < a0) a1 += twoPi;

        for (int k = 1; k <= 4; ++k) {
            float t = k / 5.0f;
            float a = a0 + (a1 - a0) * t;
            if (a >= twoPi) a -= twoPi;

            int dx = (int)std::lround(std::cos(a) * lineLen);
            int dy = (int)std::lround(std::sin(a) * lineLen);

            SDL_RenderDrawLine(renderer,
                clockCenterX, clockCenterY,
                clockCenterX + dx, clockCenterY + dy);
        }
    }
}

bool Kitchen::isInZoomClickArea(int x, int y) const {
    return isInClockHoverArea(x, y);
}

bool Kitchen::isInClockHoverArea(int x, int y) const {
    // Clock hover area centered at (530, 218)
    const int HOVER_RADIUS = 50; // Adjust this for the size of the hover area
    return (x >= 530 - HOVER_RADIUS && x <= 530 + HOVER_RADIUS &&
            y >= 218 - HOVER_RADIUS && y <= 218 + HOVER_RADIUS);
}

bool Kitchen::isInClockFace(int x, int y) const {
    if (!showingZoomedView) {
        return false;
    }

    const int clockCenterX = 503;
    const int clockCenterY = 333;
    const int minRadius = 55;
    const int maxRadius = 235;
    const int dx = x - clockCenterX;
    const int dy = y - clockCenterY;
    const int distanceSquared = (dx * dx) + (dy * dy);

    return distanceSquared >= (minRadius * minRadius) &&
           distanceSquared <= (maxRadius * maxRadius);
}

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <SDL_mixer.h>
#include <iostream>
#include <string>
#include <vector>
#include <random>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <functional>
#include <unordered_set>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <sqlite3.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "library_game.h"
#include "kitchen/kitchen.h"

struct GhostInstance;

// Forward declarations
void selectNewWordChoices();
void updateTargetWordDisplay();
void handleWordChoice(int choiceIndex);
bool loadSentences();
void cleanup();
void resetGameState();
void cycleTargetWord();
std::string maskTargetWord(const std::string& sentence, const std::string& targetWord);
void handleMouseMotion(SDL_Event& event);
bool loadTimeQuestions();
void selectRandomTimeQuestion();
void updateTimeQuestionDisplay();
void clearTimeFeedback();
void renderTimeGameUI();
bool isMouseOverRect(int mouseX, int mouseY, const SDL_Rect& rect);
void cleanupTimeTextures();
void updateTimeBlankUI();
void updateTimeDebugTexture();
void resetTimeGameState();
void startTimeGame();
void clearActiveGhosts();
void spawnGhost();
void updateGhosts();
float getGhostAlphaFactor(const GhostInstance& ghost, Uint32 currentTime);
SDL_Rect getAnimatedGhostRectForSprite(int spriteIndex, const SDL_Rect& baseRect, float baseAlphaFactor, Uint32 spawnTime, float driftVelocityX, float driftVelocityY, float& rotationOut, Uint8& alphaOut);

// Blinking animation structure
struct BlinkingSprite {
    SDL_Texture* eyesOpenTexture;
    SDL_Texture* eyesClosedTexture;
    SDL_Rect rect;
    SDL_Color color;
    Uint8 alpha;
    bool isBlinking;
    Uint32 lastBlinkTime;
    Uint32 blinkDuration;
    float rotation;
    float hoverOffset;      // New: Track vertical hover offset
    Uint32 hoverStartTime;  // New: Track hover animation time
    SDL_RendererFlip flipState; // Track flip state
    
    // Fade animation
    bool isFadingIn;
    bool isFadingOut;
    Uint32 fadeStartTime;
    Uint32 fadeDuration;
    
    // Callback for fade out completion
    std::function<void()> onFadeOutComplete;
    
    // Position presets for different screens
    struct {
        int x;
        int y;
    } positions = {
        280,  // Default x for main screen
        200   // Default y for main screen
    };
    
    struct {
        int x;
        int y;
        SDL_RendererFlip flip;
    } screenPositions[5] = {
        {280, 200, SDL_FLIP_HORIZONTAL},  // main_screen.png
        {0, 200, SDL_FLIP_HORIZONTAL},    // background.png
        {0, 220, SDL_FLIP_HORIZONTAL},     // library.png
        {680, 200, SDL_FLIP_NONE},         // attic.png
        {680, 200, SDL_FLIP_NONE}          // kitchen.png
    };
    
    void init(SDL_Renderer* renderer) {
        eyesOpenTexture = loadTexture("../assets/blinky/eyes_open.png", renderer);
        eyesClosedTexture = loadTexture("../assets/blinky/blinking.png", renderer);
        if (!eyesOpenTexture || !eyesClosedTexture) {
            printf("Failed to load eye textures!\n");
            return;
        }
        
        // Set initial values
        color = {255, 255, 255, 255};
        alpha = 0;  // Start fully transparent
        isBlinking = false;
        lastBlinkTime = SDL_GetTicks();
        blinkDuration = 200; // milliseconds
        rotation = 0.0f;
        hoverOffset = 0.0f;
        hoverStartTime = SDL_GetTicks();
        flipState = SDL_FLIP_HORIZONTAL;
        onFadeOutComplete = nullptr;
        
        // Initialize fade values
        isFadingIn = true;
        isFadingOut = false;
        fadeStartTime = SDL_GetTicks();
        fadeDuration = 1000; // 1 second fade
        
        // Set blend mode for both textures
        SDL_SetTextureBlendMode(eyesOpenTexture, SDL_BLENDMODE_BLEND);
        SDL_SetTextureBlendMode(eyesClosedTexture, SDL_BLENDMODE_BLEND);
        
        // Get texture dimensions and set initial position
        int width, height;
        SDL_QueryTexture(eyesOpenTexture, NULL, NULL, &width, &height);
        rect = {positions.x, positions.y, width, height};
    }
    
    void startFadeIn() {
        isFadingIn = true;
        isFadingOut = false;
        fadeStartTime = SDL_GetTicks();
        alpha = 0;
    }
    
    void startFadeOut(std::function<void()> callback = nullptr) {
        isFadingIn = false;
        isFadingOut = true;
        fadeStartTime = SDL_GetTicks();
        onFadeOutComplete = callback;
    }
    
    void setScreen(int screenIndex) {
        if (screenIndex >= 0 && screenIndex < 5) {
            positions.x = screenPositions[screenIndex].x;
            positions.y = screenPositions[screenIndex].y;
            flipState = screenPositions[screenIndex].flip;
            rect.x = positions.x;
            rect.y = positions.y;
            startFadeIn(); // Start fade in when changing screens
        }
    }

    void setCustomPosition(int x, int y, SDL_RendererFlip flip) {
        positions.x = x;
        positions.y = y;
        flipState = flip;
        rect.x = positions.x;
        rect.y = positions.y;
        startFadeIn();
    }
    
    void update() {
        Uint32 currentTime = SDL_GetTicks();
        
        // Update blinking
        if (isBlinking && currentTime - lastBlinkTime > blinkDuration) {
            isBlinking = false;
            lastBlinkTime = currentTime;
        } else if (!isBlinking && currentTime - lastBlinkTime > 3000) {
            isBlinking = true;
            lastBlinkTime = currentTime;
        }
        
        // Update hover animation
        float hoverPhase = (currentTime - hoverStartTime) / 2000.0f;
        hoverOffset = sinf(hoverPhase * 2 * M_PI) * 5.0f;
        rect.y = positions.y + (int)hoverOffset;
        
        // Update fade animation
        if (isFadingIn || isFadingOut) {
            float fadeProgress = (float)(currentTime - fadeStartTime) / fadeDuration;
            if (fadeProgress > 1.0f) {
                fadeProgress = 1.0f;
                if (isFadingIn) isFadingIn = false;
                if (isFadingOut) {
                    isFadingOut = false;
                    if (onFadeOutComplete) {
                        onFadeOutComplete();
                        onFadeOutComplete = nullptr;
                    }
                }
            }
            
            if (isFadingIn) {
                alpha = (Uint8)(fadeProgress * 192); // Fade in to 75% opacity
            } else if (isFadingOut) {
                alpha = (Uint8)((1.0f - fadeProgress) * 192); // Fade out from 75% opacity
            }
        }
    }
    
    void render(SDL_Renderer* renderer) {
        SDL_Texture* currentTexture = isBlinking ? eyesClosedTexture : eyesOpenTexture;
        SDL_SetTextureAlphaMod(currentTexture, alpha);
        SDL_RenderCopyEx(renderer, currentTexture, NULL, &rect, rotation, NULL, flipState);
    }
    
    void cleanup() {
        if (eyesOpenTexture) {
            SDL_DestroyTexture(eyesOpenTexture);
            eyesOpenTexture = nullptr;
        }
        if (eyesClosedTexture) {
            SDL_DestroyTexture(eyesClosedTexture);
            eyesClosedTexture = nullptr;
        }
    }
};

// Target word structure
struct TargetWord {
    int id;
    std::string word;
};

// Sentence pair structure
struct SentencePair {
    std::string spanish;
    std::string english;
    int target_word_id;
};

struct TimeQuestion {
    int id;
    int pattern_id;
    std::string promptTemplate;
    int hour;
    int minute;
    std::vector<std::string> slotAnswers;
    std::vector<int> slotAnswerValues;
};

struct TimeBlankState {
    std::string answer;
    int answerValue;
    std::vector<std::string> options;
    int selectedIndex;
    SDL_Rect viewportRect;
    int gradeState;
};

struct TimeTextSegment {
    std::string text;
    SDL_Texture* texture;
    SDL_Rect rect;
};

struct GhostInstance {
    int spriteIndex;
    SDL_Rect baseRect;
    Uint32 spawnTime;
    Uint32 fadeInDuration;
    Uint32 visibleDuration;
    Uint32 fadeOutDuration;
    float driftVelocityX;
    float driftVelocityY;
    int wordId;
    std::string word;
    SDL_Texture* wordTexture;
    SDL_Color tint;
    bool clicked;
    Uint32 clickTime;
    bool correct;
};

// Game state structure
struct GameState {
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* backgroundTexture;
    SDL_Texture* buttonTexture;
    SDL_Texture* textTexture;
    SDL_Texture* spanishTextTexture;
    SDL_Texture* englishTextTexture;
    SDL_Texture* targetWordTexture;
    SDL_Texture* timePromptTexture;
    SDL_Texture* timeCheckButtonTexture;
    SDL_Texture* timeDebugTexture;
    SDL_Texture* timeStartTextTexture;
    SDL_Texture* iconTexture;
    SDL_Texture* mainScreenTexture;
    SDL_Texture* libraryScreenTexture;
    SDL_Texture* atticTexture;  // New: Attic texture
    SDL_Texture* kitchenTexture;
    SDL_Rect kitchenRect;
    BlinkingSprite blinkingSprite;
    TTF_Font* font;
    TTF_Font* sentenceFont;
    Mix_Music* backgroundMusic;
    Mix_Chunk* correctSound;
    Mix_Chunk* incorrectSound;
    Mix_Chunk* tombstoneSound;
    std::vector<SDL_Texture*> sprites;
    std::vector<SDL_Point> spriteSizes;
    std::vector<GhostInstance> activeGhosts;
    SDL_Rect currentSpriteRect;
    SDL_Rect buttonRect;
    SDL_Rect textRect;
    SDL_Rect spanishTextRect;
    SDL_Rect englishTextRect;
    SDL_Rect targetWordRect;
    SDL_Rect timePromptRect;
    SDL_Rect timeCheckButtonRect;
    SDL_Rect timeCheckTextRect;
    SDL_Rect timeDebugRect;
    SDL_Rect timeStartButtonRect;
    SDL_Rect timeStartTextRect;
    SDL_Rect originalButtonRect;
    SDL_Rect iconRect;
    SDL_Rect mainScreenRect;
    SDL_Rect libraryScreenRect;
    SDL_Rect atticRect;  // New: Attic rect

    bool isHovered;
    bool iconHovered;
    float hoverScale;
    bool quit;
    bool gameStarted;
    int currentSprite;
    Uint32 lastSpriteChange;
    Uint32 nextGhostSpawnTime;
    Uint32 spriteStartTime;
    Uint32 currentGhostDriftStartTime;
    float fadeAlpha;
    float currentGhostDriftVelocityX;
    float currentGhostDriftVelocityY;
    bool fadingIn;
    bool waitingToFadeOut;
    std::vector<SentencePair> sentences;
    std::vector<TargetWord> target_words;
    std::vector<int> current_word_choices;
    std::vector<TimeQuestion> timeQuestions;
    std::vector<TimeBlankState> currentTimeBlanks;
    std::vector<TimeTextSegment> currentTimeSegments;
    std::vector<std::string> timeClockHourOptions;
    std::vector<std::string> timeGenericHourOptions;
    std::vector<std::string> timeMinuteOptions;
    int currentSentence;
    int nextGhostWordIndex;
    int currentTimeQuestion;
    int currentTargetWordIndex;
    bool answerFeedback;
    bool showingFeedback;
    Uint32 feedbackStartTime;
    SDL_Color feedbackColor;
    SDL_Color spriteColor;
    bool transitioningToNextRound;
    Uint32 transitionStartTime;
    bool gameFinished;
    int questionsAnswered;
    SDL_Texture* playAgainTexture;
    SDL_Texture* playAgainTextTexture;
    SDL_Rect playAgainRect;
    SDL_Rect playAgainTextRect;
    bool isPlayAgainHovered;
    bool showingIcon;
    bool showingMainScreen;
    bool showingLibraryScreen;
    bool showingGameScreen;
    bool showingAttic;  // New: Attic state
    bool showingKitchen;  // Kitchen state
    bool showingTimeFeedback;
    bool timeAnswerCorrect;
    Uint32 timeFeedbackStartTime;
    bool timeGameStarted;
};

GameState state;

// Define global variables from library_game.h
std::vector<BookSprite> bookSprites;

// Create kitchen instance
Kitchen kitchen;
bool showingBookPage = false;
SDL_Texture* bookPageTexture = nullptr;
SDL_Rect libraryBackButtonRect;
SDL_Rect bookPageBackButtonRect;
std::vector<std::string> currentBookWords;
size_t currentWordIndex = 0;
size_t lastDisplayedWord = 0;
Uint32 lastWordTime = 0;
bool showNextArrow = false;
SDL_Rect nextArrowRect;
bool isHoveringNextArrow = false;

SDL_Rect calculateCenteredRect(int textureWidth, int textureHeight, int screenWidth, int screenHeight) {
    SDL_Rect rect;
    rect.w = textureWidth;
    rect.h = textureHeight;
    rect.x = (screenWidth - textureWidth) / 2;
    rect.y = (screenHeight - textureHeight) / 2;
    return rect;
}

bool isMouseOverButton(int mouseX, int mouseY, const SDL_Rect& rect) {
    return (mouseX >= rect.x && mouseX <= rect.x + rect.w &&
            mouseY >= rect.y && mouseY <= rect.y + rect.h);
}

bool isMouseOverRect(int mouseX, int mouseY, const SDL_Rect& rect) {
    return mouseX >= rect.x && mouseX <= rect.x + rect.w &&
           mouseY >= rect.y && mouseY <= rect.y + rect.h;
}

std::string replaceAll(std::string text, const std::string& from, const std::string& to) {
    size_t startPos = 0;
    while ((startPos = text.find(from, startPos)) != std::string::npos) {
        text.replace(startPos, from.length(), to);
        startPos += to.length();
    }
    return text;
}

std::string normalizeSpanishNumberText(std::string text) {
    text = replaceAll(text, "\xC3\xA1", "a");
    text = replaceAll(text, "\xC3\xA9", "e");
    text = replaceAll(text, "\xC3\xAD", "i");
    text = replaceAll(text, "\xC3\xB3", "o");
    text = replaceAll(text, "\xC3\xBA", "u");
    text = replaceAll(text, "\xC3\xB1", "n");

    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.erase(text.begin());
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.pop_back();
    }

    return text;
}

int parseSpanishNumber(const std::string& input) {
    const std::string text = normalizeSpanishNumberText(input);
    static const std::vector<std::pair<std::string, int>> directMap = {
        {"la una", 1}, {"una", 1}, {"uno", 1}, {"dos", 2}, {"tres", 3}, {"cuatro", 4},
        {"cinco", 5}, {"seis", 6}, {"siete", 7}, {"ocho", 8}, {"nueve", 9}, {"diez", 10},
        {"once", 11}, {"doce", 12}, {"trece", 13}, {"catorce", 14}, {"quince", 15},
        {"dieciseis", 16}, {"diecisiete", 17}, {"dieciocho", 18}, {"diecinueve", 19},
        {"veinte", 20}, {"veintiuno", 21}, {"veintidos", 22}, {"veintitres", 23},
        {"veinticuatro", 24}, {"veinticinco", 25}, {"veintiseis", 26}, {"veintisiete", 27},
        {"veintiocho", 28}, {"veintinueve", 29}, {"treinta", 30}, {"cuarenta", 40},
        {"cincuenta", 50}
    };

    for (const auto& entry : directMap) {
        if (entry.first == text) {
            return entry.second;
        }
    }

    static const std::vector<std::pair<std::string, int>> tensMap = {
        {"treinta y ", 30}, {"cuarenta y ", 40}, {"cincuenta y ", 50}
    };

    for (const auto& entry : tensMap) {
        if (text.rfind(entry.first, 0) == 0) {
            const int unit = parseSpanishNumber(text.substr(entry.first.length()));
            if (unit >= 1 && unit <= 9) {
                return entry.second + unit;
            }
        }
    }

    return -1;
}

std::string fillTimePattern(std::string pattern, const std::string& firstValue, const std::string& secondValue) {
    const size_t firstBlank = pattern.find("____");
    if (firstBlank != std::string::npos) {
        pattern.replace(firstBlank, 4, firstValue);
    }

    if (!secondValue.empty()) {
        const size_t secondBlank = pattern.find("____");
        if (secondBlank != std::string::npos) {
            pattern.replace(secondBlank, 4, secondValue);
        }
    }

    return pattern;
}

void addUniqueString(std::vector<std::string>& values, const std::string& value) {
    if (std::find(values.begin(), values.end(), value) == values.end()) {
        values.push_back(value);
    }
}

std::vector<std::string> splitTimeTemplate(const std::string& patternTemplate) {
    std::vector<std::string> parts;
    size_t start = 0;
    size_t pos = patternTemplate.find("____");

    while (pos != std::string::npos) {
        parts.push_back(patternTemplate.substr(start, pos - start));
        start = pos + 4;
        pos = patternTemplate.find("____", start);
    }

    parts.push_back(patternTemplate.substr(start));
    return parts;
}

void sortTimeOptionPool(std::vector<std::string>& values) {
    std::sort(values.begin(), values.end(), [](const std::string& a, const std::string& b) {
        const int aValue = parseSpanishNumber(a);
        const int bValue = parseSpanishNumber(b);
        if (aValue != bValue) {
            return aValue < bValue;
        }
        return a < b;
    });
}

void drawButtonRect(SDL_Renderer* renderer, const SDL_Rect& rect, SDL_Color fillColor, SDL_Color outlineColor) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, fillColor.r, fillColor.g, fillColor.b, fillColor.a);
    SDL_RenderFillRect(renderer, &rect);
    SDL_SetRenderDrawColor(renderer, outlineColor.r, outlineColor.g, outlineColor.b, outlineColor.a);
    SDL_RenderDrawRect(renderer, &rect);
}

void cleanupTimeTextures() {
    state.currentTimeBlanks.clear();
    for (TimeTextSegment& segment : state.currentTimeSegments) {
        if (segment.texture) {
            SDL_DestroyTexture(segment.texture);
            segment.texture = nullptr;
        }
    }
    state.currentTimeSegments.clear();
}

void stepTimeBlank(TimeBlankState& blank, int direction) {
    if (blank.options.empty()) {
        return;
    }

    const int optionCount = static_cast<int>(blank.options.size());
    blank.selectedIndex = (blank.selectedIndex + direction + optionCount) % optionCount;
    blank.gradeState = 0;
}

void updateButtonAnimation() {
    const float TARGET_HOVER_SCALE = 1.1f;
    const float ANIMATION_SPEED = 0.1f;
    
    if (state.isHovered) {
        state.hoverScale += ANIMATION_SPEED;
        if (state.hoverScale > TARGET_HOVER_SCALE) {
            state.hoverScale = TARGET_HOVER_SCALE;
        }
    } else {
        state.hoverScale -= ANIMATION_SPEED;
        if (state.hoverScale < 1.0f) {
            state.hoverScale = 1.0f;
        }
    }
    
    int newWidth = static_cast<int>(state.originalButtonRect.w * state.hoverScale);
    int newHeight = static_cast<int>(state.originalButtonRect.h * state.hoverScale);
    
    state.buttonRect.x = state.originalButtonRect.x - (newWidth - state.originalButtonRect.w) / 2;
    state.buttonRect.y = state.originalButtonRect.y - (newHeight - state.originalButtonRect.h) / 2;
    state.buttonRect.w = newWidth;
    state.buttonRect.h = newHeight;
}

SDL_Point getRandomPosition(int spriteWidth, int spriteHeight, int screenWidth, int screenHeight) {
    const int MARGIN = 20;  // Minimum distance from screen edges
    
    std::random_device rd;
    std::mt19937 gen(rd());
    
    // Calculate available space for sprite placement
    int maxX = screenWidth - spriteWidth - MARGIN;
    int maxY = screenHeight - spriteHeight - MARGIN;
    
    // Generate random position within safe area
    std::uniform_int_distribution<> disX(MARGIN, maxX);
    std::uniform_int_distribution<> disY(MARGIN, maxY);
    
    return {disX(gen), disY(gen)};
}

void clearActiveGhosts() {
    for (GhostInstance& ghost : state.activeGhosts) {
        if (ghost.wordTexture) {
            SDL_DestroyTexture(ghost.wordTexture);
            ghost.wordTexture = nullptr;
        }
    }
    state.activeGhosts.clear();
}

void spawnGhost() {
    if (state.current_word_choices.empty() || state.target_words.empty()) {
        return;
    }

    std::unordered_set<int> activeWordIds;
    int activeChoiceCount = 0;
    for (const GhostInstance& existingGhost : state.activeGhosts) {
        if (!existingGhost.clicked) {
            activeWordIds.insert(existingGhost.wordId);
            activeChoiceCount++;
        }
    }

    if (activeChoiceCount >= 3) {
        return;
    }

    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, state.sprites.size() - 1);
    static std::uniform_real_distribution<float> driftDis(-24.0f, 24.0f);

    GhostInstance ghost{};
    ghost.spriteIndex = dis(gen);
    SDL_Point newPos = getRandomPosition(
        state.spriteSizes[ghost.spriteIndex].x,
        state.spriteSizes[ghost.spriteIndex].y,
        SCREEN_WIDTH, SCREEN_HEIGHT
    );

    ghost.baseRect.w = state.spriteSizes[ghost.spriteIndex].x;
    ghost.baseRect.h = state.spriteSizes[ghost.spriteIndex].y;
    ghost.baseRect.x = newPos.x;
    ghost.baseRect.y = newPos.y;
    ghost.spawnTime = SDL_GetTicks();
    ghost.fadeInDuration = 150;
    ghost.visibleDuration = 9000;
    ghost.fadeOutDuration = 400;
    ghost.driftVelocityX = driftDis(gen);
    ghost.driftVelocityY = driftDis(gen) * 0.7f;
    ghost.clicked = false;
    ghost.clickTime = 0;
    ghost.correct = false;
    ghost.tint = {255, 255, 255, 255};

    int wordId = -1;
    const int choiceCount = static_cast<int>(state.current_word_choices.size());
    for (int offset = 0; offset < choiceCount; ++offset) {
        const int candidateIndex = (state.nextGhostWordIndex + offset) % choiceCount;
        const int candidateWordId = state.current_word_choices[candidateIndex];
        if (!activeWordIds.count(candidateWordId)) {
            wordId = candidateWordId;
            state.nextGhostWordIndex = (candidateIndex + 1) % choiceCount;
            break;
        }
    }

    if (wordId == -1) {
        return;
    }

    ghost.wordId = wordId;
    ghost.wordTexture = nullptr;

    auto wordIt = std::find_if(state.target_words.begin(), state.target_words.end(),
        [wordId](const TargetWord& tw) { return tw.id == wordId; });
    if (wordIt != state.target_words.end()) {
        ghost.word = wordIt->word;
    }

    const int totalWidth = std::max(10, static_cast<int>(ghost.word.length()));
    const int paddingNeeded = totalWidth - static_cast<int>(ghost.word.length());
    const int leftPadding = paddingNeeded / 2;
    const int rightPadding = paddingNeeded - leftPadding;
    const std::string paddedWord = std::string(leftPadding, ' ') + ghost.word + std::string(rightPadding, ' ');
    SDL_Surface* textSurface = TTF_RenderUTF8_Shaded(state.font, paddedWord.c_str(), {0, 0, 0, 255}, {255, 255, 255, 255});
    if (textSurface) {
        ghost.wordTexture = SDL_CreateTextureFromSurface(state.renderer, textSurface);
        if (ghost.wordTexture) {
            SDL_SetTextureBlendMode(ghost.wordTexture, SDL_BLENDMODE_BLEND);
        }
        SDL_FreeSurface(textSurface);
    }

    state.activeGhosts.push_back(ghost);
    state.lastSpriteChange = ghost.spawnTime;
    std::cout << "Spawned ghost count: " << state.activeGhosts.size() << std::endl;
}

float getGhostAlphaFactor(const GhostInstance& ghost, Uint32 currentTime) {
    if (ghost.clicked) {
        const float clickElapsed = static_cast<float>(currentTime - ghost.clickTime);
        return std::clamp(1.0f - (clickElapsed / ghost.fadeOutDuration), 0.0f, 1.0f);
    }

    const Uint32 age = (currentTime >= ghost.spawnTime) ? (currentTime - ghost.spawnTime) : 0;
    if (age < ghost.fadeInDuration) {
        return static_cast<float>(age) / ghost.fadeInDuration;
    }
    if (age < ghost.visibleDuration) {
        return 1.0f;
    }
    const float fadeElapsed = static_cast<float>(age - ghost.visibleDuration);
    return std::clamp(1.0f - (fadeElapsed / ghost.fadeOutDuration), 0.0f, 1.0f);
}

void updateGhosts() {
    Uint32 currentTime = SDL_GetTicks();
    const Uint32 spawnInterval = 1500;

    if (!state.gameStarted || state.showingFeedback || state.gameFinished) {
        return;
    }

    if (state.nextGhostSpawnTime == 0) {
        state.nextGhostSpawnTime = currentTime;
    }

    while (currentTime >= state.nextGhostSpawnTime) {
        spawnGhost();
        state.nextGhostSpawnTime += spawnInterval;
    }

    currentTime = SDL_GetTicks();

    state.activeGhosts.erase(
        std::remove_if(state.activeGhosts.begin(), state.activeGhosts.end(),
            [currentTime](GhostInstance& ghost) {
                const float alpha = getGhostAlphaFactor(ghost, currentTime);
                if (alpha <= 0.0f) {
                    if (ghost.wordTexture) {
                        SDL_DestroyTexture(ghost.wordTexture);
                        ghost.wordTexture = nullptr;
                    }
                    return true;
                }
                return false;
            }),
        state.activeGhosts.end());
}

SDL_Rect getAnimatedGhostRectForSprite(
    int spriteIndex,
    const SDL_Rect& baseRect,
    float baseAlphaFactor,
    Uint32 spawnTime,
    float driftVelocityX,
    float driftVelocityY,
    float& rotationOut,
    Uint8& alphaOut
) {
    SDL_Rect animatedRect = baseRect;
    rotationOut = 0.0f;
    alphaOut = static_cast<Uint8>(std::clamp(baseAlphaFactor * 192.0f, 0.0f, 255.0f));

    if (spriteIndex < 0 || spriteIndex >= static_cast<int>(state.sprites.size())) {
        return animatedRect;
    }

    const float t = SDL_GetTicks() / 1000.0f;
    const float driftElapsed = (SDL_GetTicks() - spawnTime) / 1000.0f;
    const float spritePhase = (spriteIndex * 0.9f) + (baseRect.x * 0.01f);

    const float yOffset = sinf((t * 2.1f) + spritePhase) * 7.0f;
    const float xOffset = sinf((t * 1.35f) + (spritePhase * 0.7f)) * 5.0f;
    const float sharedRipple = sinf((t * 1.6f) + (baseRect.x * 0.0125f)) * 4.0f;

    animatedRect.x += static_cast<int>(std::lround((driftVelocityX * driftElapsed) + xOffset + sharedRipple));
    animatedRect.y += static_cast<int>(std::lround((driftVelocityY * driftElapsed) + yOffset));

    rotationOut = sinf((t * 1.2f) + (spritePhase * 0.8f)) * 4.0f;

    const float alphaPulse = 0.9f + (0.1f * ((sinf((t * 1.7f) + spritePhase) + 1.0f) * 0.5f));
    alphaOut = static_cast<Uint8>(std::clamp(baseAlphaFactor * 192.0f * alphaPulse, 0.0f, 255.0f));

    return animatedRect;
}

SDL_Texture* createTextTexture(const std::string& text, SDL_Color color, int wrapWidth = 0) {
    SDL_Surface* surface = nullptr;
    
    if (wrapWidth > 0) {
        // Use wrapped text rendering if wrap width is specified
        surface = TTF_RenderUTF8_Blended_Wrapped(state.sentenceFont, text.c_str(), color, wrapWidth);
    } else {
        // Use regular text rendering if no wrap width specified
        surface = TTF_RenderUTF8_Blended(state.font, text.c_str(), color);
    }
    
    if (!surface) {
        std::cout << "Failed to render text surface! SDL_ttf Error: " << TTF_GetError() << std::endl;
        return nullptr;
    }

    SDL_Texture* texture = SDL_CreateTextureFromSurface(state.renderer, surface);
    SDL_FreeSurface(surface);
    
    if (!texture) {
        std::cout << "Failed to create texture from rendered text! SDL Error: " << SDL_GetError() << std::endl;
        return nullptr;
    }
    
    return texture;
}

void updateSentenceDisplay() {
    if (state.spanishTextTexture) {
        SDL_DestroyTexture(state.spanishTextTexture);
    }
    if (state.englishTextTexture) {
        SDL_DestroyTexture(state.englishTextTexture);
    }

    // Create semi-transparent white background for text
    SDL_SetRenderDrawBlendMode(state.renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(state.renderer, 255, 255, 255, 200); // White with 78% opacity

    // Calculate wrap width - use ~60% of screen width for better readability
    const int TEXT_MARGIN = 40;  // 20 pixels on each side
    const float WIDTH_PERCENTAGE = 0.6f;  // Use 60% of screen width
    const int WRAP_WIDTH = static_cast<int>(SCREEN_WIDTH * WIDTH_PERCENTAGE) - (TEXT_MARGIN * 2);

    // Find the target word for the current sentence
    int target_word_id = state.sentences[state.currentSentence].target_word_id;
    auto it = std::find_if(state.target_words.begin(), state.target_words.end(),
                          [target_word_id](const TargetWord& tw) { return tw.id == target_word_id; });
    
    // Spanish text with masked target word
    SDL_Color textColor = {0, 0, 0, 255}; // Black text
    std::string maskedSpanish = state.sentences[state.currentSentence].spanish;
    if (it != state.target_words.end()) {
        maskedSpanish = maskTargetWord(state.sentences[state.currentSentence].spanish, it->word);
    }
    
    // Use smaller font for sentences
    SDL_Surface* spanishSurface = TTF_RenderUTF8_Blended_Wrapped(state.sentenceFont, 
        maskedSpanish.c_str(), textColor, WRAP_WIDTH);
    if (!spanishSurface) {
        std::cout << "Failed to create Spanish text surface! SDL_ttf Error: " << TTF_GetError() << std::endl;
        return;
    }
    state.spanishTextTexture = SDL_CreateTextureFromSurface(state.renderer, spanishSurface);
    SDL_FreeSurface(spanishSurface);
    
    int textWidth, textHeight;
    SDL_QueryTexture(state.spanishTextTexture, nullptr, nullptr, &textWidth, &textHeight);
    state.spanishTextRect = {
        (SCREEN_WIDTH - textWidth) / 2,  // Center horizontally
        40,                              // Higher up from top
        textWidth,
        textHeight
    };

    // Background rect for Spanish text (slightly larger)
    SDL_Rect spanishBgRect = {
        state.spanishTextRect.x - 10,
        state.spanishTextRect.y - 5,
        state.spanishTextRect.w + 20,
        state.spanishTextRect.h + 10
    };

    // English text with masked target word
    std::string maskedEnglish = state.sentences[state.currentSentence].english;
    if (it != state.target_words.end()) {
        maskedEnglish = maskTargetWord(state.sentences[state.currentSentence].english, it->word);
    }
    
    // Use smaller font for sentences
    SDL_Surface* englishSurface = TTF_RenderUTF8_Blended_Wrapped(state.sentenceFont, 
        maskedEnglish.c_str(), textColor, WRAP_WIDTH);
    if (!englishSurface) {
        std::cout << "Failed to create English text surface! SDL_ttf Error: " << TTF_GetError() << std::endl;
        return;
    }
    state.englishTextTexture = SDL_CreateTextureFromSurface(state.renderer, englishSurface);
    SDL_FreeSurface(englishSurface);
    
    SDL_QueryTexture(state.englishTextTexture, nullptr, nullptr, &textWidth, &textHeight);
    state.englishTextRect = {
        (SCREEN_WIDTH - textWidth) / 2,  // Center horizontally
        state.spanishTextRect.y + state.spanishTextRect.h + 30,  // 30 pixels below Spanish text
        textWidth,
        textHeight
    };

    // Background rect for English text (slightly larger)
    SDL_Rect englishBgRect = {
        state.englishTextRect.x - 10,
        state.englishTextRect.y - 5,
        state.englishTextRect.w + 20,
        state.englishTextRect.h + 10
    };

    // Draw the background rectangles
    SDL_RenderFillRect(state.renderer, &spanishBgRect);
    SDL_RenderFillRect(state.renderer, &englishBgRect);
}

std::string maskTargetWord(const std::string& sentence, const std::string& targetWord) {
    std::string lowercaseSentence = sentence;
    std::string lowercaseTarget = targetWord;
    
    // Convert both strings to lowercase for case-insensitive comparison
    std::transform(lowercaseSentence.begin(), lowercaseSentence.end(), lowercaseSentence.begin(), ::tolower);
    std::transform(lowercaseTarget.begin(), lowercaseTarget.end(), lowercaseTarget.begin(), ::tolower);
    
    // Find the target word in the sentence
    size_t pos = lowercaseSentence.find(lowercaseTarget);
    if (pos != std::string::npos) {
        // Replace with original sentence but mask the target word
        std::string result = sentence;
        result.replace(pos, targetWord.length(), "____");
        return result;
    }
    return sentence;
}

void selectNewWordChoices() {
    state.current_word_choices.clear();
    state.nextGhostWordIndex = 0;
    
    // Get the target word ID from the current sentence
    int target_word_id = state.sentences[state.currentSentence].target_word_id;
    std::cout << "Target word ID for sentence: " << target_word_id << std::endl;
    
    // Find and print the target word
    auto target_it = std::find_if(state.target_words.begin(), state.target_words.end(),
                              [target_word_id](const TargetWord& tw) { return tw.id == target_word_id; });
    if (target_it != state.target_words.end()) {
        std::cout << "Target word: " << target_it->word << std::endl;
    }
    
    // Create a vector of all possible word IDs except the target word
    std::vector<int> available_word_ids;
    for (const auto& word : state.target_words) {
        if (word.id != target_word_id) {
            available_word_ids.push_back(word.id);
        }
    }
    
    // Randomly select exactly 3 words from the available words
    std::random_device rd;
    std::mt19937 gen(rd());
    std::shuffle(available_word_ids.begin(), available_word_ids.end(), gen);
    
    // Add 3 random wrong choices
    for (int i = 0; i < 3 && i < available_word_ids.size(); ++i) {
        state.current_word_choices.push_back(available_word_ids[i]);
    }
    
    // Add the target word
    state.current_word_choices.push_back(target_word_id);

    // Make sure we have exactly 4 choices
    if (state.current_word_choices.size() != 4) {
        std::cout << "Error: Wrong number of word choices generated!" << std::endl;
        return;
    }
    
    // Shuffle all choices including the target word
    std::shuffle(state.current_word_choices.begin(), state.current_word_choices.end(), gen);
    
    // Print out all word choices for debugging
    std::cout << "Word choices: ";
    for (int id : state.current_word_choices) {
        auto word_it = std::find_if(state.target_words.begin(), state.target_words.end(),
                                  [id](const TargetWord& tw) { return tw.id == id; });
        if (word_it != state.target_words.end()) {
            std::cout << word_it->word << "(" << id << ") ";
        }
    }
    std::cout << std::endl;
}

void updateTargetWordDisplay() {
    if (state.targetWordTexture) {
        SDL_DestroyTexture(state.targetWordTexture);
        state.targetWordTexture = nullptr;
    }

    if (state.current_word_choices.empty() || 
        state.currentTargetWordIndex >= state.current_word_choices.size()) {
        return;
    }

    // Get the current word
    int wordId = state.current_word_choices[state.currentTargetWordIndex];
    std::string word;
    auto word_it = std::find_if(state.target_words.begin(), state.target_words.end(),
                               [wordId](const TargetWord& tw) { return tw.id == wordId; });
    
    if (word_it == state.target_words.end()) {
        std::cout << "Word ID " << wordId << " not found!" << std::endl;
        return;
    }
    word = word_it->word;

    // Pad the word to be at least 10 characters
    int totalWidth = std::max(10, static_cast<int>(word.length()));
    int currentWidth = word.length();
    int paddingNeeded = totalWidth - currentWidth;
    int leftPadding = paddingNeeded / 2;
    int rightPadding = paddingNeeded - leftPadding;
    
    std::string paddedWord = std::string(leftPadding, ' ') + word + std::string(rightPadding, ' ');

    // Create white background color and black text color
    SDL_Color bgColor = {255, 255, 255, 255};  // White
    SDL_Color textColor = {0, 0, 0, 255};      // Black

    // First create a surface with white background
    SDL_Surface* textSurface = TTF_RenderUTF8_Shaded(state.font, paddedWord.c_str(), textColor, bgColor);
    if (!textSurface) {
        std::cout << "Failed to render text surface! SDL_ttf Error: " << TTF_GetError() << std::endl;
        return;
    }

    state.targetWordTexture = SDL_CreateTextureFromSurface(state.renderer, textSurface);
    
    // Update the target word rectangle size based on the surface
    state.targetWordRect.w = textSurface->w;
    state.targetWordRect.h = textSurface->h;
    
    SDL_FreeSurface(textSurface);

    if (!state.targetWordTexture) {
        std::cout << "Failed to create texture from rendered text! SDL Error: " << SDL_GetError() << std::endl;
        return;
    }

    // Only position the target word if we have a valid sprite
    if (state.currentSprite < state.sprites.size()) {
        // Position the target word in the center of the sprite
        state.targetWordRect.x = state.currentSpriteRect.x + (state.currentSpriteRect.w - state.targetWordRect.w) / 2;
        state.targetWordRect.y = state.currentSpriteRect.y + (state.currentSpriteRect.h - state.targetWordRect.h) / 2;
    }
}

void cycleTargetWord() {
    if (state.current_word_choices.empty()) {
        return;
    }
    // Move to the next word in the choices
    state.currentTargetWordIndex = (state.currentTargetWordIndex + 1) % state.current_word_choices.size();
    std::cout << "Cycling to word index " << state.currentTargetWordIndex 
              << " (word ID: " << state.current_word_choices[state.currentTargetWordIndex] << ")" << std::endl;
    updateTargetWordDisplay();
}

void updatePlayAgainButton() {
    if (!state.gameFinished) return;

    // Create play again text if it doesn't exist
    if (!state.playAgainTexture) {
        // Load tombstone texture if not already loaded
        SDL_Surface* tombstoneSurface = IMG_Load("../assets/tombstone.png");
        if (!tombstoneSurface) {
            std::cout << "Failed to load tombstone image! SDL_image Error: " << IMG_GetError() << std::endl;
            return;
        }

        state.playAgainTexture = SDL_CreateTextureFromSurface(state.renderer, tombstoneSurface);
        if (!state.playAgainTexture) {
            std::cout << "Failed to create tombstone texture! SDL Error: " << SDL_GetError() << std::endl;
            SDL_FreeSurface(tombstoneSurface);
            return;
        }

        // Set the play again button position and size to match the play button
        state.playAgainRect.w = 100;  // Match play button width
        state.playAgainRect.h = 100;  // Match play button height
        SDL_FreeSurface(tombstoneSurface);

        int windowWidth, windowHeight;
        SDL_GetWindowSize(state.window, &windowWidth, &windowHeight);
        state.playAgainRect.x = (windowWidth - state.playAgainRect.w) / 2;
        state.playAgainRect.y = (windowHeight - state.playAgainRect.h) / 2;

        // Create "Play Again" text with gray color
        SDL_Color textColor = {64, 64, 64, 255};  // Same gray as play button
        SDL_Surface* playAgainTextSurface = TTF_RenderText_Blended_Wrapped(state.font, "Play\nAgain", textColor, 100);
        if (!playAgainTextSurface) {
            std::cout << "Failed to create play again text surface! SDL_ttf Error: " << TTF_GetError() << std::endl;
            return;
        }

        state.playAgainTextTexture = SDL_CreateTextureFromSurface(state.renderer, playAgainTextSurface);
        if (!state.playAgainTextTexture) {
            std::cout << "Failed to create play again text texture! SDL Error: " << SDL_GetError() << std::endl;
            SDL_FreeSurface(playAgainTextSurface);
            return;
        }

        // Set up text position
        state.playAgainTextRect.w = playAgainTextSurface->w;
        state.playAgainTextRect.h = playAgainTextSurface->h;
        // Add a 5-pixel offset to the right to better center on the tombstone
        state.playAgainTextRect.x = state.playAgainRect.x + (state.playAgainRect.w - playAgainTextSurface->w) / 2 + 5;
        // Move text up slightly and adjust vertical position
        state.playAgainTextRect.y = state.playAgainRect.y + (state.playAgainRect.h - playAgainTextSurface->h) / 2 - 15;

        SDL_FreeSurface(playAgainTextSurface);
    }

    // Handle hover effect
    int mouseX, mouseY;
    SDL_GetMouseState(&mouseX, &mouseY);
    bool wasHovered = state.isPlayAgainHovered;
    state.isPlayAgainHovered = mouseX >= state.playAgainRect.x && mouseX <= state.playAgainRect.x + state.playAgainRect.w &&
                              mouseY >= state.playAgainRect.y && mouseY <= state.playAgainRect.y + state.playAgainRect.h;

    // Apply hover scale effect
    if (state.isPlayAgainHovered) {
        if (!wasHovered) {
            state.hoverScale = 1.1f;  // Start scaling up
        }
        else if (state.hoverScale > 1.0f) {
            state.hoverScale = std::max(1.0f, state.hoverScale - 0.01f);  // Gradually return to normal
        }
    }
    else {
        state.hoverScale = 1.0f;
    }

    // Apply scale to the button rectangle for rendering
    SDL_Rect scaledRect = state.playAgainRect;
    int originalWidth = scaledRect.w;
    int originalHeight = scaledRect.h;
    scaledRect.w = static_cast<int>(originalWidth * state.hoverScale);
    scaledRect.h = static_cast<int>(originalHeight * state.hoverScale);
    scaledRect.x = (state.playAgainRect.x + originalWidth/2) - scaledRect.w/2;
    scaledRect.y = (state.playAgainRect.y + originalHeight/2) - scaledRect.h/2;

    SDL_RenderCopy(state.renderer, state.playAgainTexture, NULL, &scaledRect);
    SDL_RenderCopy(state.renderer, state.playAgainTextTexture, NULL, &state.playAgainTextRect);
}

void handleEvents(SDL_Event& e, GameState& state) {
    if (e.type == SDL_QUIT) {
        state.quit = true;
    }
    else if (e.type == SDL_MOUSEMOTION) {
        handleMouseMotion(e);
    }
    else if (e.type == SDL_MOUSEBUTTONDOWN) {
        if (e.button.button == SDL_BUTTON_LEFT) {
            int mouseX = e.button.x;
            int mouseY = e.button.y;
            std::cout << "DEBUG: Mouse click at: (" << mouseX << "," << mouseY << ")" << std::endl;
            
            // Reset cursor to arrow after any click
            SDL_SetCursor(SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW));
            
            bool inBottomLeftCorner = (mouseX < 100 && mouseY > SCREEN_HEIGHT - 100);
            
            if (state.showingIcon) {
                // Check if click is in center of icon
                int centerX = state.iconRect.x + state.iconRect.w/2;
                int centerY = state.iconRect.y + state.iconRect.h/2;
                int hoverRadius = 100; // Click area radius
                
                if (abs(mouseX - centerX) < hoverRadius && 
                    abs(mouseY - centerY) < hoverRadius) {
                    state.showingIcon = false;
                    state.showingMainScreen = true;
                    state.blinkingSprite.setScreen(0); // Set to main screen position
                    // Stop background music when transitioning from icon screen
                    Mix_HaltMusic();
                    // Reset cursor when transitioning from icon screen
                    state.iconHovered = false;
                    SDL_SetCursor(SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW));
                }
            }
            // Handle library screen clicks
            else if (state.showingLibraryScreen) {
                if (showingBookPage) {
                    // In book page view, only check for book page back button
                    if (isMouseOverBookPageBack(mouseX, mouseY)) {
                        showingBookPage = false;
                    } else if (showNextArrow && isMouseOverNextArrow(mouseX, mouseY)) {  // Only handle click if arrow is visible
                        handleNextArrowClick();
                    }
                } else {
                    // In library view, check for library back button or book clicks
                    if (inBottomLeftCorner) {
                        // Start fade out and transition to main screen when complete
                        state.blinkingSprite.startFadeOut([&]() {
                            state.showingLibraryScreen = false;
                            state.showingMainScreen = true;
                            state.blinkingSprite.setScreen(0); // Set to main screen position
                        });
                    } else {
                        handleBookClick(mouseX, mouseY);
                    }
                }
            }
            // Handle game screen back button
            else if (state.showingGameScreen && inBottomLeftCorner) {
                if (!state.gameStarted) {
                    // Start fade out and transition to main screen when complete
                    state.blinkingSprite.startFadeOut([&]() {
                        state.showingGameScreen = false;
                        state.showingMainScreen = true;
                        state.blinkingSprite.setScreen(0); // Set to main screen position
                        resetGameState();
                        Mix_HaltMusic();
                    });
                } else {
                    // If game is started, just transition directly without fade
                    state.showingGameScreen = false;
                    state.showingMainScreen = true;
                    state.blinkingSprite.setScreen(0); // Set to main screen position
                    resetGameState();
                    Mix_HaltMusic();
                }
            }
            // Handle main screen navigation
            else if (state.showingMainScreen) {
                // Check for attic click (top of staircase)
                if (mouseX >= 400 && mouseX <= 600 && mouseY >= 100 && mouseY <= 200) {
                    std::cout << "Attic click detected at: " << mouseX << "," << mouseY << std::endl;
                    // Start fade out and transition to attic when complete
                    state.blinkingSprite.startFadeOut([&]() {
                        std::cout << "Transitioning to attic..." << std::endl;
                        state.showingMainScreen = false;
                        state.showingAttic = true;
                        state.blinkingSprite.setScreen(3); // Set to attic position
                    });
                }
                // Check for library click
                else if (mouseX >= 675 && mouseX <= 763 && mouseY >= 338 && mouseY <= 558) {
                    std::cout << "Library click at: (" << mouseX << "," << mouseY << ")" << std::endl;
                    // Start fade out and transition to library when complete
                    state.blinkingSprite.startFadeOut([&]() {
                        state.showingMainScreen = false;
                        state.showingLibraryScreen = true;
                        state.blinkingSprite.setScreen(2); // Set to library position
                        initBackButtons();
                    });
                }
                // Check for kitchen click
                else if (mouseX >= 894 && mouseX <= 994 && mouseY >= 667 && mouseY <= 767) {
                    std::cout << "Kitchen click at: (" << mouseX << "," << mouseY << ")" << std::endl;
                    // Start fade out and transition to kitchen when complete
                    state.blinkingSprite.startFadeOut([&]() {
                        std::cout << "Transitioning to kitchen..." << std::endl;
                        state.showingMainScreen = false;
                        state.showingKitchen = true;
                        state.blinkingSprite.setScreen(4); // Set to kitchen position
                    });
                }
                // Check for game area click (bottom left corner)
                else if (mouseX <= 100 && mouseY >= SCREEN_HEIGHT - 100) {
                    // Start fade out and transition to game when complete
                    state.blinkingSprite.startFadeOut([&]() {
                        state.showingMainScreen = false;
                        state.showingGameScreen = true;
                        state.blinkingSprite.setScreen(1); // Set to game/background position
                        Mix_PlayMusic(state.backgroundMusic, -1);
                    });
                }
            } else if (state.showingGameScreen && !state.gameStarted && isMouseOverButton(mouseX, mouseY, state.buttonRect)) {
                std::cout << "Button clicked!" << std::endl;
                state.gameStarted = true;
                state.showingGameScreen = true;  // Add this line
                Mix_PlayChannel(-1, state.tombstoneSound, 0);  // Play the tombstone sound once
                // Select initial word choices when game starts
                selectNewWordChoices();
                updateSentenceDisplay();
                clearActiveGhosts();
                state.lastSpriteChange = 0;
                state.nextGhostSpawnTime = SDL_GetTicks() + 1500;
                state.currentSprite = state.sprites.size();
                state.nextGhostWordIndex = 0;
                spawnGhost();
                spawnGhost();
            }
            else if (state.gameStarted && !state.showingFeedback) {
                for (auto it = state.activeGhosts.rbegin(); it != state.activeGhosts.rend(); ++it) {
                    GhostInstance& ghost = *it;
                    float ghostRotation = 0.0f;
                    Uint8 ghostAlpha = 0;
                    SDL_Rect animatedGhostRect = getAnimatedGhostRectForSprite(
                        ghost.spriteIndex,
                        ghost.baseRect,
                        getGhostAlphaFactor(ghost, SDL_GetTicks()),
                        ghost.spawnTime,
                        ghost.driftVelocityX,
                        ghost.driftVelocityY,
                        ghostRotation,
                        ghostAlpha
                    );

                    if (mouseX >= animatedGhostRect.x &&
                        mouseX <= animatedGhostRect.x + animatedGhostRect.w &&
                        mouseY >= animatedGhostRect.y &&
                        mouseY <= animatedGhostRect.y + animatedGhostRect.h &&
                        !ghost.clicked) {
                        const int target_word_id = state.sentences[state.currentSentence].target_word_id;
                        const bool isCorrect = ghost.wordId == target_word_id;

                        ghost.clicked = true;
                        ghost.clickTime = SDL_GetTicks();
                        ghost.correct = isCorrect;
                        ghost.tint = isCorrect ? SDL_Color{255, 255, 200, 255} : SDL_Color{255, 200, 200, 255};

                        if (isCorrect) {
                            const Uint32 clickTime = ghost.clickTime;
                            for (GhostInstance& otherGhost : state.activeGhosts) {
                                if (&otherGhost == &ghost) {
                                    continue;
                                }
                                otherGhost.clicked = true;
                                otherGhost.clickTime = clickTime;
                                otherGhost.tint = {255, 255, 255, 255};
                            }
                            state.answerFeedback = true;
                            state.showingFeedback = true;
                            state.feedbackStartTime = SDL_GetTicks();
                            Mix_PlayChannel(-1, state.correctSound, 0);
                            state.questionsAnswered++;

                            if (state.questionsAnswered >= 10) {
                                state.gameFinished = true;
                                clearActiveGhosts();
                                if (state.spanishTextTexture) {
                                    SDL_DestroyTexture(state.spanishTextTexture);
                                    state.spanishTextTexture = nullptr;
                                }
                                if (state.englishTextTexture) {
                                    SDL_DestroyTexture(state.englishTextTexture);
                                    state.englishTextTexture = nullptr;
                                }
                                state.spanishTextRect = {0, 0, 0, 0};
                                state.englishTextRect = {0, 0, 0, 0};
                            }
                        } else {
                            Mix_PlayChannel(-1, state.incorrectSound, 0);
                        }
                        break;
                    }
                }
            }
            else if (state.gameFinished && state.isPlayAgainHovered) {
                // Reset game state
                state.gameFinished = false;
                state.questionsAnswered = 0;
                state.currentSentence = 0;
                state.showingFeedback = false;
                state.answerFeedback = false;
                state.transitioningToNextRound = false;
                state.transitionStartTime = 0;
                state.spriteColor = {255, 255, 255, 255};
                state.hoverScale = 1.0f;
                state.isHovered = false;
                state.isPlayAgainHovered = false;
                updateSentenceDisplay();
                selectNewWordChoices();
                clearActiveGhosts();
                state.lastSpriteChange = 0;
                state.nextGhostSpawnTime = SDL_GetTicks() + 1500;
                state.nextGhostWordIndex = 0;
                spawnGhost();
                spawnGhost();
            }
            else if (state.showingLibraryScreen) {
                if (showingBookPage) {
                    // In book page view, only check for book page back button
                    if (isMouseOverBookPageBack(mouseX, mouseY)) {
                        showingBookPage = false;
                    } else if (showNextArrow && isMouseOverNextArrow(mouseX, mouseY)) {  // Only handle click if arrow is visible
                        handleNextArrowClick();
                    }
                } else {
                    // In library view, check for library back button or book clicks
                    if (inBottomLeftCorner) {
                        // Start fade out and transition to main screen when complete
                        state.blinkingSprite.startFadeOut([&]() {
                            state.showingLibraryScreen = false;
                            state.showingMainScreen = true;
                            state.blinkingSprite.setScreen(0); // Set to main screen position
                        });
                    } else {
                        handleBookClick(mouseX, mouseY);
                    }
                }
            }
            else if (state.showingKitchen) {
                if (inBottomLeftCorner) {
                    if (kitchen.isZoomed()) {
                        // Start fade out and transition back to kitchen view
                        state.blinkingSprite.startFadeOut([&]() {
                            std::cout << "Transitioning back to kitchen view..." << std::endl;
                            kitchen.toggleZoom();
                            resetTimeGameState();
                            state.blinkingSprite.setScreen(4); // Back to kitchen position
                        });
                    } else {
                        // Start fade out and transition to main screen when complete
                        state.blinkingSprite.startFadeOut([&]() {
                            std::cout << "Transitioning back to main screen from kitchen..." << std::endl;
                            resetTimeGameState();
                            state.showingKitchen = false;
                            state.showingMainScreen = true;
                            state.blinkingSprite.setScreen(0); // Set to main screen position
                        });
                    }
                } else if (kitchen.isInClockHoverArea(mouseX, mouseY) && !kitchen.isZoomed()) {
                    // Start fade out and transition to clock zoom view
                    state.blinkingSprite.startFadeOut([&]() {
                        std::cout << "Transitioning to clock zoom view..." << std::endl;
                        kitchen.toggleZoom();
                        resetTimeGameState();
                        state.blinkingSprite.setCustomPosition(20, 200, SDL_FLIP_HORIZONTAL);
                    });
                } else if (kitchen.isZoomed() && !state.timeGameStarted) {
                    if (isMouseOverRect(mouseX, mouseY, state.timeStartButtonRect)) {
                        startTimeGame();
                    }
                } else if (kitchen.isZoomed()) {
                    bool handledBlankClick = false;
                    for (TimeBlankState& blank : state.currentTimeBlanks) {
                        if (isMouseOverRect(mouseX, mouseY, blank.viewportRect) && !blank.options.empty()) {
                            const int midpointY = blank.viewportRect.y + (blank.viewportRect.h / 2);
                            if (mouseY < midpointY) {
                                stepTimeBlank(blank, -1);
                            } else {
                                stepTimeBlank(blank, 1);
                            }
                            handledBlankClick = true;
                            break;
                        }
                    }

                    if (handledBlankClick) {
                        // No-op
                    } else if (isMouseOverRect(mouseX, mouseY, state.timeCheckButtonRect) &&
                        state.currentTimeQuestion >= 0 &&
                        state.currentTimeQuestion < static_cast<int>(state.timeQuestions.size())) {
                        const TimeQuestion& question = state.timeQuestions[state.currentTimeQuestion];
                        state.timeAnswerCorrect = state.currentTimeBlanks.size() == question.slotAnswers.size();
                        for (size_t i = 0; i < state.currentTimeBlanks.size(); ++i) {
                            const bool isCorrect = state.currentTimeBlanks[i].options[state.currentTimeBlanks[i].selectedIndex] == question.slotAnswers[i];
                            state.currentTimeBlanks[i].gradeState = isCorrect ? 1 : -1;
                            if (!isCorrect) {
                                state.timeAnswerCorrect = false;
                            }
                        }

                        if (state.timeAnswerCorrect) {
                            state.showingTimeFeedback = true;
                            state.timeFeedbackStartTime = SDL_GetTicks();
                            Mix_PlayChannel(-1, state.correctSound, 0);
                        } else {
                            state.showingTimeFeedback = false;
                            Mix_PlayChannel(-1, state.incorrectSound, 0);
                        }
                    }
                }
            }
            else if (state.showingAttic && inBottomLeftCorner) {
                // Start fade out and transition to main screen when complete
                state.blinkingSprite.startFadeOut([&]() {
                    std::cout << "Transitioning back to main screen from attic..." << std::endl;
                    state.showingAttic = false;
                    state.showingMainScreen = true;
                    state.blinkingSprite.setScreen(0); // Set to main screen position
                });
            }
        }
    }
    else if (e.type == SDL_MOUSEWHEEL) {
    if (state.showingKitchen && kitchen.isZoomed() && state.timeGameStarted) {
        int mouseX = 0;
        int mouseY = 0;
        SDL_GetMouseState(&mouseX, &mouseY);
        for (TimeBlankState& blank : state.currentTimeBlanks) {
            if (isMouseOverRect(mouseX, mouseY, blank.viewportRect) && !blank.options.empty()) {
                    stepTimeBlank(blank, e.wheel.y > 0 ? -1 : 1);
                    break;
                }
            }
        }
    }
}

void resetGameState() {
    state.gameStarted = false;
    state.gameFinished = false;
    state.questionsAnswered = 0;
    state.currentSentence = 0;
    state.showingFeedback = false;
    state.answerFeedback = false;
    state.transitioningToNextRound = false;
    state.currentSprite = state.sprites.size();
    state.lastSpriteChange = 0;
    state.nextGhostSpawnTime = 0;
    state.nextGhostWordIndex = 0;
    clearActiveGhosts();
    
    // Clear any existing textures
    if (state.spanishTextTexture) {
        SDL_DestroyTexture(state.spanishTextTexture);
        state.spanishTextTexture = nullptr;
    }
    if (state.englishTextTexture) {
        SDL_DestroyTexture(state.englishTextTexture);
        state.englishTextTexture = nullptr;
    }
    if (state.playAgainTexture) {
        SDL_DestroyTexture(state.playAgainTexture);
        state.playAgainTexture = nullptr;
    }
    if (state.playAgainTextTexture) {
        SDL_DestroyTexture(state.playAgainTextTexture);
        state.playAgainTextTexture = nullptr;
    }
}

void mainLoop() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        handleEvents(e, state);
    }
    
    // Handle feedback and transitions
    if (state.showingFeedback) {
        Uint32 currentTime = SDL_GetTicks();
        if (currentTime - state.feedbackStartTime > 1500) { // Show feedback for 1.5 seconds
            state.showingFeedback = false;
            
            // If answer was correct, move to next round
            if (state.answerFeedback && !state.gameFinished) {
                state.transitioningToNextRound = true;
                state.transitionStartTime = currentTime;
            }
        }
    }
    
    if (state.transitioningToNextRound && 
        SDL_GetTicks() - state.transitionStartTime > 500) { // Wait 0.5 seconds before next round
        state.transitioningToNextRound = false;
        state.currentSentence = rand() % state.sentences.size();
        updateSentenceDisplay();
        selectNewWordChoices();
        clearActiveGhosts();
        state.lastSpriteChange = 0;
        state.nextGhostSpawnTime = SDL_GetTicks() + 1500;
        state.nextGhostWordIndex = 0;
        spawnGhost();
        spawnGhost();
    }

    if (state.showingTimeFeedback) {
        Uint32 currentTime = SDL_GetTicks();
        if (currentTime - state.timeFeedbackStartTime > 1200) {
            const bool wasCorrect = state.timeAnswerCorrect;
            clearTimeFeedback();
            if (wasCorrect) {
                selectRandomTimeQuestion();
            }
        }
    }
    
    updateButtonAnimation();
    updateGhosts();

    if (state.showingLibraryScreen && showingBookPage) {
        updateTextAnimation();  // Add this line to update text animation
    }

    // Clear screen
    SDL_SetRenderDrawColor(state.renderer, 255, 255, 255, 255);
    SDL_RenderClear(state.renderer);
    
    if (state.showingIcon) {
        SDL_RenderCopy(state.renderer, state.iconTexture, NULL, &state.iconRect);
    } else if (state.showingMainScreen) {
        SDL_RenderCopy(state.renderer, state.mainScreenTexture, NULL, &state.mainScreenRect);
        
        // Handle blinking and fade animation
        state.blinkingSprite.update();
        state.blinkingSprite.render(state.renderer);
        
    } else if (state.showingLibraryScreen) {
        SDL_RenderCopy(state.renderer, state.libraryScreenTexture, NULL, &state.libraryScreenRect);
        
        // Render eyes in library before the books
        state.blinkingSprite.update();
        state.blinkingSprite.render(state.renderer);
        
        // Render books on top of the eyes
        renderBookSprites(state.renderer);
        
    } else if (state.showingAttic) {
        SDL_RenderCopy(state.renderer, state.atticTexture, NULL, &state.atticRect);
        
        // Handle blinking and fade animation in attic
        state.blinkingSprite.update();
        state.blinkingSprite.render(state.renderer);
    } else if (state.showingKitchen) {
        SDL_RenderCopy(state.renderer, state.kitchenTexture, NULL, &state.kitchenRect);
        kitchen.updateClockHands();
        kitchen.render(state.renderer);
        renderTimeGameUI();
        state.blinkingSprite.update();
        state.blinkingSprite.render(state.renderer);
        
    } else if (state.showingGameScreen) {
        // Always render the background first
        SDL_RenderCopy(state.renderer, state.backgroundTexture, NULL, NULL);
        
        if (!state.gameStarted) {
            SDL_RenderCopy(state.renderer, state.buttonTexture, NULL, &state.buttonRect);
            SDL_RenderCopy(state.renderer, state.textTexture, NULL, &state.textRect);
            
            // Only render eyes before the game starts
            state.blinkingSprite.update();
            state.blinkingSprite.render(state.renderer);
        } else {
            // Draw semi-transparent white backgrounds for sentences
            SDL_SetRenderDrawBlendMode(state.renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(state.renderer, 255, 255, 255, 200); // White with 78% opacity
            
            // Add some padding around the text
            const int padding = 10;
            SDL_Rect spanishBgRect = {
                state.spanishTextRect.x - padding,
                state.spanishTextRect.y - padding,
                state.spanishTextRect.w + (2 * padding),
                state.spanishTextRect.h + (2 * padding)
            };
            
            SDL_Rect englishBgRect = {
                state.englishTextRect.x - padding,
                state.englishTextRect.y - padding,
                state.englishTextRect.w + (2 * padding),
                state.englishTextRect.h + (2 * padding)
            };
            
            SDL_RenderFillRect(state.renderer, &spanishBgRect);
            SDL_RenderFillRect(state.renderer, &englishBgRect);
            
            // Reset blend mode
            SDL_SetRenderDrawBlendMode(state.renderer, SDL_BLENDMODE_NONE);
            
            SDL_RenderCopy(state.renderer, state.spanishTextTexture, NULL, &state.spanishTextRect);
            SDL_RenderCopy(state.renderer, state.englishTextTexture, NULL, &state.englishTextRect);
            
            for (const GhostInstance& ghost : state.activeGhosts) {
                float ghostRotation = 0.0f;
                Uint8 ghostAlpha = 0;
                SDL_Rect animatedGhostRect = getAnimatedGhostRectForSprite(
                    ghost.spriteIndex,
                    ghost.baseRect,
                    getGhostAlphaFactor(ghost, SDL_GetTicks()),
                    ghost.spawnTime,
                    ghost.driftVelocityX,
                    ghost.driftVelocityY,
                    ghostRotation,
                    ghostAlpha
                );

                SDL_SetTextureAlphaMod(state.sprites[ghost.spriteIndex], ghostAlpha);
                SDL_SetTextureColorMod(state.sprites[ghost.spriteIndex],
                                      ghost.tint.r,
                                      ghost.tint.g,
                                      ghost.tint.b);
                SDL_RenderCopyEx(state.renderer, state.sprites[ghost.spriteIndex],
                                 NULL, &animatedGhostRect, ghostRotation, NULL, SDL_FLIP_NONE);

                if (ghost.wordTexture) {
                    SDL_SetTextureAlphaMod(ghost.wordTexture, ghostAlpha);
                    int wordWidth = 0;
                    int wordHeight = 0;
                    SDL_QueryTexture(ghost.wordTexture, nullptr, nullptr, &wordWidth, &wordHeight);
                    SDL_Rect animatedWordRect = {
                        animatedGhostRect.x + (animatedGhostRect.w - wordWidth) / 2,
                        animatedGhostRect.y + (animatedGhostRect.h - wordHeight) / 2,
                        wordWidth,
                        wordHeight
                    };
                    SDL_RenderCopy(state.renderer, ghost.wordTexture, NULL, &animatedWordRect);
                }
            }
        }
    }
    
    if (state.gameFinished) {
        updatePlayAgainButton();
    }
    
    SDL_RenderPresent(state.renderer);
}

void handleMouseMotion(SDL_Event& event) {
    int mouseX, mouseY;
    SDL_GetMouseState(&mouseX, &mouseY);
    std::cout << "DEBUG: Mouse click at: (" << mouseX << "," << mouseY << ")" << std::endl;
    std::cout << "Click at: (" << mouseX << "," << mouseY << ")" << std::endl;
    bool inBottomLeftCorner = mouseX >= 0 && mouseX <= 100 && mouseY >= SCREEN_HEIGHT - 100 && mouseY <= SCREEN_HEIGHT;

    SDL_Cursor* cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);

    if (state.showingMainScreen) {
        // Check for attic area hover (top of staircase)
        if (mouseX >= 400 && mouseX <= 600 && mouseY >= 100 && mouseY <= 200) {
            cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
        }
        // Check for kitchen area hover
        else if (mouseX >= 894 && mouseX <= 994 && mouseY >= 667 && mouseY <= 767) {
            cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
        }
        // Check for library area hover (matches click area)
        else if (mouseX >= 675 && mouseX <= 763 && mouseY >= 338 && mouseY <= 558) {
            cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
        }
        // Check for game/background area hover (bottom left corner)
        else if (mouseX <= 100 && mouseY >= SCREEN_HEIGHT - 100) {
            cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
        }
    } else if (state.showingKitchen) {
        // Show hand cursor for clock area and back button
        if (!kitchen.isZoomed() && kitchen.isInClockHoverArea(mouseX, mouseY)) {
            cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
        } else if (kitchen.isZoomed() && !state.timeGameStarted &&
                   isMouseOverRect(mouseX, mouseY, state.timeStartButtonRect)) {
            cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
        } else if (kitchen.isZoomed() && state.timeGameStarted &&
                   isMouseOverRect(mouseX, mouseY, state.timeCheckButtonRect)) {
            cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
        } else if (kitchen.isZoomed() && state.timeGameStarted) {
            for (const TimeBlankState& blank : state.currentTimeBlanks) {
                if (isMouseOverRect(mouseX, mouseY, blank.viewportRect)) {
                    cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
                    break;
                }
            }
        } else if (inBottomLeftCorner) {
            cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
        }
    } else if (state.showingAttic) {
        // Show hand cursor for back button
        if (inBottomLeftCorner) {
            cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
        }
    } else if (state.showingLibraryScreen) {
        // Show hand cursor for back button and book areas
        if (inBottomLeftCorner) {
            cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
        } else if (!showingBookPage) {
            // Check for book hover areas when not showing a book page
            for (const BookSprite& book : bookSprites) {
                if (mouseX >= book.rect.x && mouseX < book.rect.x + book.rect.w &&
                    mouseY >= book.rect.y && mouseY < book.rect.y + book.rect.h) {
                    cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
                    break;
                }
            }
        }
    }

    // Handle other interactive elements
    if (state.showingIcon) {
        int centerX = state.iconRect.x + state.iconRect.w/2;
        int centerY = state.iconRect.y + state.iconRect.h/2;
        int hoverRadius = 100;
        
        if (abs(mouseX - centerX) < hoverRadius && abs(mouseY - centerY) < hoverRadius) {
            cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
            state.iconHovered = true;
        } else {
            state.iconHovered = false;
        }
    }

    if (state.showingGameScreen && !state.gameStarted) {
        if (isMouseOverButton(mouseX, mouseY, state.buttonRect)) {
            cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
            state.isHovered = true;
        } else {
            state.isHovered = false;
        }
    }

    if (showingBookPage && isMouseOverBookPageBack(mouseX, mouseY)) {
        cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
    }

    if (showNextArrow) {
        if (mouseX >= nextArrowRect.x && mouseX < nextArrowRect.x + nextArrowRect.w &&
            mouseY >= nextArrowRect.y && mouseY < nextArrowRect.y + nextArrowRect.h) {
            cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
            isHoveringNextArrow = true;
        } else {
            isHoveringNextArrow = false;
        }
    }

    SDL_SetCursor(cursor);
}

bool loadSentences() {
    sqlite3* db;
#ifdef __EMSCRIPTEN__
    int rc = sqlite3_open("data/spanish_game.db", &db);
#else
    int rc = sqlite3_open("../data/spanish_game.db", &db);
#endif
    if (rc) {
        std::cout << "Can't open database: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }

    // First load all target words
    const char* words_sql = "SELECT id, word FROM target_words;";
    sqlite3_stmt* words_stmt;
    rc = sqlite3_prepare_v2(db, words_sql, -1, &words_stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cout << "Failed to fetch target words: " << sqlite3_errmsg(db) << std::endl;
        sqlite3_close(db);
        return false;
    }

    while (sqlite3_step(words_stmt) == SQLITE_ROW) {
        TargetWord word;
        word.id = sqlite3_column_int(words_stmt, 0);
        word.word = reinterpret_cast<const char*>(sqlite3_column_text(words_stmt, 1));
        state.target_words.push_back(word);
    }
    sqlite3_finalize(words_stmt);

    std::cout << "Loaded " << state.target_words.size() << " target words" << std::endl;

    // Get a count of total sentences first
    const char* count_sql = "SELECT COUNT(*) FROM sentences;";
    sqlite3_stmt* count_stmt;
    rc = sqlite3_prepare_v2(db, count_sql, -1, &count_stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cout << "Failed to count sentences: " << sqlite3_errmsg(db) << std::endl;
        sqlite3_close(db);
        return false;
    }

    int total_sentences = 0;
    if (sqlite3_step(count_stmt) == SQLITE_ROW) {
        total_sentences = sqlite3_column_int(count_stmt, 0);
        std::cout << "Total sentences in database: " << total_sentences << std::endl;
    }
    sqlite3_finalize(count_stmt);

    // Use a better random selection method and include target_word_id
    const char* sql = "SELECT spanish_text, english_text, target_word_id FROM sentences ORDER BY RANDOM();";
    sqlite3_stmt* stmt;
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    
    if (rc != SQLITE_OK) {
        std::cout << "Failed to fetch data: " << sqlite3_errmsg(db) << std::endl;
        sqlite3_close(db);
        return false;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        SentencePair pair;
        pair.spanish = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        pair.english = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        pair.target_word_id = sqlite3_column_int(stmt, 2);
        state.sentences.push_back(pair);
    }

    std::cout << "Loaded " << state.sentences.size() << " sentences" << std::endl;

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    if (state.sentences.empty()) {
        std::cout << "No sentences were loaded!" << std::endl;
        return false;
    }

    // Initialize with a random sentence
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, state.sentences.size() - 1);
    state.currentSentence = dis(gen);
    
    // Initialize the first set of word choices
    selectNewWordChoices();

    return true;
}

bool loadTimeQuestions() {
    sqlite3* db;
#ifdef __EMSCRIPTEN__
    int rc = sqlite3_open("data/spanish_game.db", &db);
#else
    int rc = sqlite3_open("../data/spanish_game.db", &db);
#endif
    if (rc) {
        std::cout << "Can't open database for time questions: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }

    const char* sql =
        "SELECT e.id, e.pattern_id, p.pattern_template, e.hour, e.minute "
        "FROM time_expressions e "
        "JOIN time_patterns p ON p.id = e.pattern_id "
        "ORDER BY RANDOM();";

    sqlite3_stmt* stmt;
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cout << "Failed to fetch time questions: " << sqlite3_errmsg(db) << std::endl;
        sqlite3_close(db);
        return false;
    }

    state.timeQuestions.clear();
    state.timeClockHourOptions.clear();
    state.timeGenericHourOptions.clear();
    state.timeMinuteOptions.clear();

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const int id = sqlite3_column_int(stmt, 0);
        const int patternId = sqlite3_column_int(stmt, 1);
        const char* patternText = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        const char* firstText = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        const char* secondText = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));

        const std::string patternTemplate = patternText ? patternText : "";
        const std::string firstValue = firstText ? firstText : "";
        const std::string secondValue = secondText ? secondText : "";

        int hour = -1;
        int minute = 0;

        switch (patternId) {
            case 1:
            case 5:
                hour = parseSpanishNumber(firstValue);
                minute = 0;
                break;
            case 2:
            case 7:
                hour = parseSpanishNumber(firstValue);
                minute = parseSpanishNumber(secondValue);
                break;
            case 3:
            case 4: {
                const int minutesToHour = parseSpanishNumber(firstValue);
                const int upcomingHour = parseSpanishNumber(secondValue);
                hour = upcomingHour - 1;
                if (hour == 0) {
                    hour = 12;
                }
                minute = 60 - minutesToHour;
                break;
            }
            case 6: {
                const int displayedHour = parseSpanishNumber(firstValue);
                const int minutesToHour = parseSpanishNumber(secondValue);
                hour = displayedHour - 1;
                if (hour == 0) {
                    hour = 12;
                }
                minute = 60 - minutesToHour;
                break;
            }
            default:
                break;
        }

        if (hour < 1 || hour > 12 || minute < 0 || minute > 59) {
            continue;
        }

        TimeQuestion question;
        question.id = id;
        question.pattern_id = patternId;
        question.promptTemplate = patternTemplate;
        question.hour = hour;
        question.minute = minute;

        switch (patternId) {
            case 1:
            case 5:
                question.slotAnswers = {firstValue};
                addUniqueString(state.timeClockHourOptions, firstValue);
                break;
            case 2:
            case 7:
                question.slotAnswers = {firstValue, secondValue};
                addUniqueString(state.timeGenericHourOptions, firstValue);
                addUniqueString(state.timeMinuteOptions, secondValue);
                break;
            case 3:
            case 4:
                question.slotAnswers = {firstValue, secondValue};
                addUniqueString(state.timeMinuteOptions, firstValue);
                addUniqueString(state.timeGenericHourOptions, secondValue);
                break;
            case 6:
                question.slotAnswers = {firstValue, secondValue};
                addUniqueString(state.timeGenericHourOptions, firstValue);
                addUniqueString(state.timeMinuteOptions, secondValue);
                break;
            default:
                break;
        }

        state.timeQuestions.push_back(question);
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    sortTimeOptionPool(state.timeClockHourOptions);
    sortTimeOptionPool(state.timeGenericHourOptions);
    sortTimeOptionPool(state.timeMinuteOptions);

    std::cout << "Loaded " << state.timeQuestions.size() << " time questions" << std::endl;
    return !state.timeQuestions.empty();
}

void clearTimeFeedback() {
    for (TimeBlankState& blank : state.currentTimeBlanks) {
        blank.gradeState = 0;
    }
    state.showingTimeFeedback = false;
    state.timeAnswerCorrect = false;
    state.timeFeedbackStartTime = 0;
}

void updateTimeBlankUI() {
    cleanupTimeTextures();

    if (state.currentTimeQuestion < 0 || state.currentTimeQuestion >= static_cast<int>(state.timeQuestions.size())) {
        return;
    }

    const TimeQuestion& question = state.timeQuestions[state.currentTimeQuestion];
    const std::vector<std::string> parts = splitTimeTemplate(question.promptTemplate);
    if (parts.size() != question.slotAnswers.size() + 1) {
        return;
    }

    const int blankWidth = 196;
    const int blankHeight = 72;
    const int targetScrollerCenterX = 645;
    const int targetScrollerCenterY = 650;
    const int gapAfterBlank = 4;

    std::vector<int> partWidths(parts.size(), 0);
    int totalWidth = 0;
    for (size_t i = 0; i < parts.size(); ++i) {
        int textWidth = 0;
        int textHeight = 0;
        TTF_SizeUTF8(state.font, parts[i].c_str(), &textWidth, &textHeight);
        partWidths[i] = textWidth;
        totalWidth += textWidth;
    }
    totalWidth += static_cast<int>(question.slotAnswers.size()) * blankWidth;
    totalWidth += static_cast<int>(question.slotAnswers.size()) * gapAfterBlank;

    int cursorX = (SCREEN_WIDTH - totalWidth) / 2;
    const int baselineY = targetScrollerCenterY - (blankHeight / 2);

    for (size_t i = 0; i < question.slotAnswers.size(); ++i) {
        if (!parts[i].empty()) {
            TimeTextSegment segment;
            segment.text = parts[i];
            segment.texture = createTextTexture(segment.text, {20, 20, 20, 255});
            if (segment.texture) {
                int textWidth = 0;
                int textHeight = 0;
                SDL_QueryTexture(segment.texture, nullptr, nullptr, &textWidth, &textHeight);
                segment.rect = {cursorX, baselineY + (blankHeight / 2) - (textHeight / 2), textWidth, textHeight};
                state.currentTimeSegments.push_back(segment);
            }
            cursorX += partWidths[i];
        }

        TimeBlankState blank;
        blank.answer = question.slotAnswers[i];
        blank.answerValue = parseSpanishNumber(question.slotAnswers[i]);
        blank.selectedIndex = 0;
        blank.gradeState = 0;

        const bool useClockHours = (question.pattern_id == 1 || question.pattern_id == 5) && i == 0;
        const bool useMinutePool = (question.pattern_id == 2 || question.pattern_id == 3 ||
                                    question.pattern_id == 4 || question.pattern_id == 6 ||
                                    question.pattern_id == 7) &&
                                   ((question.pattern_id == 2 || question.pattern_id == 7 || question.pattern_id == 6) ? i == 1 : i == 0);

        if (useClockHours) {
            blank.options = state.timeClockHourOptions;
        } else if (useMinutePool) {
            blank.options = state.timeMinuteOptions;
        } else {
            blank.options = state.timeGenericHourOptions;
        }

        if (blank.options.empty()) {
            cursorX += blankWidth;
            continue;
        }

        auto selectedIt = std::find(blank.options.begin(), blank.options.end(), blank.answer);
        if (selectedIt != blank.options.end()) {
            blank.selectedIndex = static_cast<int>(std::distance(blank.options.begin(), selectedIt));
        }

        blank.viewportRect = {cursorX, baselineY, blankWidth, blankHeight};

        state.currentTimeBlanks.push_back(blank);
        cursorX += blankWidth + gapAfterBlank;
    }

    if (!parts.back().empty()) {
        TimeTextSegment segment;
        segment.text = parts.back();
        segment.texture = createTextTexture(segment.text, {20, 20, 20, 255});
        if (segment.texture) {
            int textWidth = 0;
            int textHeight = 0;
            SDL_QueryTexture(segment.texture, nullptr, nullptr, &textWidth, &textHeight);
            segment.rect = {cursorX, baselineY + (blankHeight / 2) - (textHeight / 2), textWidth, textHeight};
            state.currentTimeSegments.push_back(segment);
        }
    }

    if (!state.currentTimeBlanks.empty()) {
        const SDL_Rect& firstBlank = state.currentTimeBlanks.front().viewportRect;
        const SDL_Rect& lastBlank = state.currentTimeBlanks.back().viewportRect;
        const int scrollerLeft = firstBlank.x;
        const int scrollerRight = lastBlank.x + lastBlank.w;
        const int currentScrollerCenterX = (scrollerLeft + scrollerRight) / 2;
        const int shiftX = targetScrollerCenterX - currentScrollerCenterX;

        for (TimeBlankState& blank : state.currentTimeBlanks) {
            blank.viewportRect.x += shiftX;
        }
        for (TimeTextSegment& segment : state.currentTimeSegments) {
            segment.rect.x += shiftX;
        }

        const SDL_Rect& shiftedFirstBlank = state.currentTimeBlanks.front().viewportRect;
        const SDL_Rect& shiftedLastBlank = state.currentTimeBlanks.back().viewportRect;
        int leftEdge = shiftedFirstBlank.x;
        int rightEdge = shiftedLastBlank.x + shiftedLastBlank.w;
        for (const TimeTextSegment& segment : state.currentTimeSegments) {
            leftEdge = std::min(leftEdge, segment.rect.x);
            rightEdge = std::max(rightEdge, segment.rect.x + segment.rect.w);
        }

        state.timePromptRect = {
            leftEdge,
            shiftedFirstBlank.y,
            rightEdge - leftEdge,
            blankHeight
        };
        state.timeCheckButtonRect.x = shiftedLastBlank.x + shiftedLastBlank.w + 24;
        state.timeCheckButtonRect.y = shiftedFirstBlank.y + (shiftedFirstBlank.h - state.timeCheckButtonRect.h) / 2;
        state.timeCheckTextRect.x = state.timeCheckButtonRect.x + (state.timeCheckButtonRect.w - state.timeCheckTextRect.w) / 2;
        state.timeCheckTextRect.y = state.timeCheckButtonRect.y + (state.timeCheckButtonRect.h - state.timeCheckTextRect.h) / 2;
    }
}

void updateTimeQuestionDisplay() {
    if (state.timePromptTexture) {
        SDL_DestroyTexture(state.timePromptTexture);
        state.timePromptTexture = nullptr;
    }

    if (state.currentTimeQuestion < 0 || state.currentTimeQuestion >= static_cast<int>(state.timeQuestions.size())) {
        return;
    }

    updateTimeBlankUI();
    updateTimeDebugTexture();
}

void updateTimeDebugTexture() {
    if (state.timeDebugTexture) {
        SDL_DestroyTexture(state.timeDebugTexture);
        state.timeDebugTexture = nullptr;
    }

    if (state.currentTimeQuestion < 0 || state.currentTimeQuestion >= static_cast<int>(state.timeQuestions.size())) {
        return;
    }

    const TimeQuestion& question = state.timeQuestions[state.currentTimeQuestion];
    std::ostringstream stream;
    stream << "DB time "
           << question.hour << ':'
           << std::setw(2) << std::setfill('0') << question.minute
           << " | display "
           << kitchen.getDisplayedHour() << ':'
           << std::setw(2) << std::setfill('0') << kitchen.getDisplayedMinute()
           << " | minute angle " << std::fixed << std::setprecision(1) << kitchen.getMinuteHandRotation()
           << " | hour angle " << std::fixed << std::setprecision(1) << kitchen.getHourHandRotation();

    state.timeDebugTexture = createTextTexture(stream.str(), {20, 20, 20, 255}, 820);
    if (!state.timeDebugTexture) {
        return;
    }

    int width = 0;
    int height = 0;
    SDL_QueryTexture(state.timeDebugTexture, nullptr, nullptr, &width, &height);
    state.timeDebugRect = {40, SCREEN_HEIGHT - 48, width, height};
}

void selectRandomTimeQuestion() {
    if (state.timeQuestions.empty()) {
        state.currentTimeQuestion = -1;
        return;
    }

    state.currentTimeQuestion = rand() % state.timeQuestions.size();
    clearTimeFeedback();
    const TimeQuestion& question = state.timeQuestions[state.currentTimeQuestion];
    kitchen.setDisplayedTime(question.hour, question.minute);
    updateTimeQuestionDisplay();
}

void resetTimeGameState() {
    state.timeGameStarted = false;
    state.currentTimeQuestion = -1;
    clearTimeFeedback();
    cleanupTimeTextures();
    if (state.timePromptTexture) {
        SDL_DestroyTexture(state.timePromptTexture);
        state.timePromptTexture = nullptr;
    }
    if (state.timeDebugTexture) {
        SDL_DestroyTexture(state.timeDebugTexture);
        state.timeDebugTexture = nullptr;
    }
    kitchen.resetDisplayedTime();
    Mix_HaltMusic();
}

void startTimeGame() {
    state.timeGameStarted = true;
    Mix_PlayMusic(state.backgroundMusic, -1);
    selectRandomTimeQuestion();
}

void renderTimeGameUI() {
    if (!kitchen.isZoomed()) {
        return;
    }

    if (!state.timeGameStarted) {
        SDL_RenderCopy(state.renderer, state.buttonTexture, nullptr, &state.timeStartButtonRect);
        SDL_RenderCopy(state.renderer, state.timeStartTextTexture, nullptr, &state.timeStartTextRect);
        if (state.timeDebugTexture) {
            SDL_Rect debugPanel = {
                state.timeDebugRect.x - 12,
                state.timeDebugRect.y - 8,
                state.timeDebugRect.w + 24,
                state.timeDebugRect.h + 16
            };
            SDL_SetRenderDrawBlendMode(state.renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(state.renderer, 255, 255, 255, 220);
            SDL_RenderFillRect(state.renderer, &debugPanel);
            SDL_SetRenderDrawColor(state.renderer, 220, 220, 220, 255);
            SDL_RenderDrawRect(state.renderer, &debugPanel);
            SDL_RenderCopy(state.renderer, state.timeDebugTexture, nullptr, &state.timeDebugRect);
        }
        return;
    }

    updateTimeDebugTexture();

    SDL_SetRenderDrawBlendMode(state.renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(state.renderer, 255, 248, 235, 230);

    SDL_Rect promptPanel = {
        state.timePromptRect.x - 20,
        state.timePromptRect.y - 12,
        state.timePromptRect.w + 40,
        state.timePromptRect.h + 24
    };
    SDL_RenderFillRect(state.renderer, &promptPanel);

    SDL_SetRenderDrawColor(state.renderer, 231, 212, 181, 255);
    SDL_RenderDrawRect(state.renderer, &promptPanel);

    for (const TimeTextSegment& segment : state.currentTimeSegments) {
        if (segment.texture) {
            SDL_RenderCopy(state.renderer, segment.texture, nullptr, &segment.rect);
        }
    }

    for (const TimeBlankState& blank : state.currentTimeBlanks) {
        SDL_Color blankFill = {248, 244, 238, 240};
        SDL_Color centerFill = {230, 216, 195, 230};
        if (blank.gradeState == 1) {
            blankFill = {255, 255, 200, 255};
            centerFill = {255, 245, 170, 255};
        } else if (blank.gradeState == -1) {
            blankFill = {255, 200, 200, 255};
            centerFill = {255, 176, 176, 255};
        }

        SDL_SetRenderDrawColor(state.renderer, blankFill.r, blankFill.g, blankFill.b, blankFill.a);
        SDL_RenderFillRect(state.renderer, &blank.viewportRect);
        SDL_SetRenderDrawColor(state.renderer, 220, 220, 220, 255);
        SDL_RenderDrawRect(state.renderer, &blank.viewportRect);

        SDL_Rect centerBand = {
            blank.viewportRect.x + 2,
            blank.viewportRect.y + (blank.viewportRect.h / 2) - 16,
            blank.viewportRect.w - 4,
            32
        };
        SDL_SetRenderDrawColor(state.renderer, centerFill.r, centerFill.g, centerFill.b, centerFill.a);
        SDL_RenderFillRect(state.renderer, &centerBand);

        SDL_RenderSetClipRect(state.renderer, &blank.viewportRect);

        const int optionCount = static_cast<int>(blank.options.size());
        const int itemSpacing = 26;
        for (int offset = -1; offset <= 1; ++offset) {
            const int optionIndex = (blank.selectedIndex + offset + optionCount) % optionCount;
            SDL_Color textColor = offset == 0 ? SDL_Color{20, 20, 20, 255} : SDL_Color{110, 110, 110, 255};
            SDL_Texture* optionTexture = createTextTexture(blank.options[optionIndex], textColor);
            if (!optionTexture) {
                continue;
            }

            int textWidth = 0;
            int textHeight = 0;
            SDL_QueryTexture(optionTexture, nullptr, nullptr, &textWidth, &textHeight);
            SDL_Rect textRect = {
                blank.viewportRect.x + (blank.viewportRect.w - textWidth) / 2,
                blank.viewportRect.y + (blank.viewportRect.h / 2) - (textHeight / 2) + (offset * itemSpacing),
                textWidth,
                textHeight
            };
            SDL_RenderCopy(state.renderer, optionTexture, nullptr, &textRect);
            SDL_DestroyTexture(optionTexture);
        }

        SDL_RenderSetClipRect(state.renderer, nullptr);

        SDL_SetRenderDrawBlendMode(state.renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(state.renderer, blankFill.r, blankFill.g, blankFill.b, 180);
        SDL_Rect topFade = {blank.viewportRect.x, blank.viewportRect.y, blank.viewportRect.w, 16};
        SDL_Rect bottomFade = {blank.viewportRect.x, blank.viewportRect.y + blank.viewportRect.h - 16, blank.viewportRect.w, 16};
        SDL_RenderFillRect(state.renderer, &topFade);
        SDL_RenderFillRect(state.renderer, &bottomFade);
    }

    drawButtonRect(state.renderer, state.timeCheckButtonRect, {255, 255, 255, 255}, {40, 40, 40, 255});
    if (state.timeCheckButtonTexture) {
        SDL_RenderCopy(state.renderer, state.timeCheckButtonTexture, nullptr, &state.timeCheckTextRect);
    }

    if (state.timeDebugTexture) {
        SDL_Rect debugPanel = {
            state.timeDebugRect.x - 12,
            state.timeDebugRect.y - 8,
            state.timeDebugRect.w + 24,
            state.timeDebugRect.h + 16
        };
        SDL_SetRenderDrawColor(state.renderer, 255, 255, 255, 220);
        SDL_RenderFillRect(state.renderer, &debugPanel);
        SDL_SetRenderDrawColor(state.renderer, 220, 220, 220, 255);
        SDL_RenderDrawRect(state.renderer, &debugPanel);
        SDL_RenderCopy(state.renderer, state.timeDebugTexture, nullptr, &state.timeDebugRect);
    }
}

int main(int argc, char* argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        std::cout << "SDL could not initialize! SDL_Error: " << SDL_GetError() << std::endl;
        return 1;
    }

    // Initialize SDL_mixer
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
        std::cout << "SDL_mixer could not initialize! SDL_mixer Error: " << Mix_GetError() << std::endl;
        return 1;
    }

    if (TTF_Init() == -1) {
        std::cout << "SDL_ttf could not initialize! SDL_ttf Error: " << TTF_GetError() << std::endl;
        return 1;
    }

    int imgFlags = IMG_INIT_PNG;
    if (!(IMG_Init(imgFlags) & imgFlags)) {
        std::cout << "SDL_image could not initialize! SDL_image Error: " << IMG_GetError() << std::endl;
        return 1;
    }

    state.window = SDL_CreateWindow("Spanish Preposition Game", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 
                                  SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_SHOWN);
    if (state.window == NULL) {
        std::cout << "Window could not be created! SDL_Error: " << SDL_GetError() << std::endl;
        return 1;
    }

    state.renderer = SDL_CreateRenderer(state.window, -1, SDL_RENDERER_ACCELERATED);
    if (state.renderer == NULL) {
        std::cout << "Renderer could not be created! SDL_Error: " << SDL_GetError() << std::endl;
        return 1;
    }

    state.backgroundTexture = loadTexture("../assets/background.png", state.renderer);
    if (!state.backgroundTexture) {
        std::cerr << "Failed to load background texture!" << std::endl;
        return 1;
    }

    // Get the background texture dimensions
    int bgWidth, bgHeight;
    SDL_QueryTexture(state.backgroundTexture, nullptr, nullptr, &bgWidth, &bgHeight);

    state.buttonTexture = loadTexture("../assets/tombstone.png", state.renderer);
    if (!state.buttonTexture) {
        return 1;
    }

    state.font = TTF_OpenFont("../assets/Arial.ttf", 24);
    if (!state.font) {
        std::cout << "Failed to load font! SDL_ttf Error: " << TTF_GetError() << std::endl;
        return 1;
    }

    state.sentenceFont = TTF_OpenFont("../assets/Arial.ttf", 18);
    if (!state.sentenceFont) {
        std::cout << "Failed to load sentence font! SDL_ttf Error: " << TTF_GetError() << std::endl;
        return 1;
    }

    SDL_Color textColor = {64, 64, 64, 255};
    SDL_Surface* textSurface = TTF_RenderText_Blended(state.font, "Play", textColor);
    if (!textSurface) {
        std::cout << "Failed to create text surface! SDL_ttf Error: " << TTF_GetError() << std::endl;
        return 1;
    }

    state.textTexture = SDL_CreateTextureFromSurface(state.renderer, textSurface);
    if (!state.textTexture) {
        std::cout << "Failed to create text texture! SDL Error: " << SDL_GetError() << std::endl;
        SDL_FreeSurface(textSurface);
        return 1;
    }

    state.originalButtonRect.w = 100;
    state.originalButtonRect.h = 100;
    state.originalButtonRect.x = (SCREEN_WIDTH - state.originalButtonRect.w) / 2;
    state.originalButtonRect.y = (SCREEN_HEIGHT - state.originalButtonRect.h) / 2;
    
    state.buttonRect = state.originalButtonRect;
    state.isHovered = false;
    state.hoverScale = 1.0f;

    state.textRect.w = textSurface->w;
    state.textRect.h = textSurface->h;
    state.textRect.x = state.originalButtonRect.x + (state.originalButtonRect.w - textSurface->w) / 2;
    state.textRect.y = state.originalButtonRect.y + (state.originalButtonRect.h - textSurface->h) / 2;

    SDL_FreeSurface(textSurface);

    state.quit = false;

    // Load background music
    state.backgroundMusic = Mix_LoadMUS("../assets/background_music.ogg");
    if (!state.backgroundMusic) {
        std::cout << "Failed to load background music! SDL_mixer Error: " << Mix_GetError() << std::endl;
        return 1;
    }

    // Start playing background music immediately
    Mix_PlayMusic(state.backgroundMusic, -1);  // -1 means loop indefinitely

    // Load sound effects
    state.correctSound = Mix_LoadWAV("../assets/correct.ogg");
    if (!state.correctSound) {
        std::cout << "Failed to load correct sound effect! SDL_mixer Error: " << Mix_GetError() << std::endl;
        return 1;
    }
    
    state.incorrectSound = Mix_LoadWAV("../assets/incorrect.ogg");
    if (!state.incorrectSound) {
        std::cout << "Failed to load incorrect sound effect! SDL_mixer Error: " << Mix_GetError() << std::endl;
        return 1;
    }
    
    state.tombstoneSound = Mix_LoadWAV("../assets/tombstone.ogg");
    if (!state.tombstoneSound) {
        std::cout << "Failed to load tombstone sound effect! SDL_mixer Error: " << Mix_GetError() << std::endl;
        return 1;
    }
    Mix_VolumeChunk(state.tombstoneSound, MIX_MAX_VOLUME); // Set to maximum volume

    std::vector<std::string> spriteFiles = {
        "../assets/sprite1.png",
        "../assets/sprite2.png",
        "../assets/sprite3.png"
    };

    for (const auto& file : spriteFiles) {
        SDL_Texture* sprite = loadTexture(file.c_str(), state.renderer);
        if (!sprite) {
            return 1;
        }
        
        // Get sprite dimensions
        int width, height;
        SDL_QueryTexture(sprite, nullptr, nullptr, &width, &height);
        state.spriteSizes.push_back({width, height});
        
        state.sprites.push_back(sprite);
    }

    // Set blend mode and opacity for sprites
    for (auto& sprite : state.sprites) {
        SDL_SetTextureBlendMode(sprite, SDL_BLENDMODE_BLEND);
        SDL_SetTextureAlphaMod(sprite, 192);  // 75% opacity (192 out of 255)
    }

    // Initialize sprite color to white
    state.spriteColor = {255, 255, 255, 255};
    state.transitioningToNextRound = false;
    state.transitionStartTime = 0;

    // Load icon texture
    std::string iconPath = "../assets/icon.png";
    SDL_Surface* iconSurface = IMG_Load(iconPath.c_str());
    if (!iconSurface) {
        std::cerr << "Failed to load icon image: " << IMG_GetError() << std::endl;
        return 1;
    }
    state.iconTexture = SDL_CreateTextureFromSurface(state.renderer, iconSurface);
    SDL_FreeSurface(iconSurface);
    if (!state.iconTexture) {
        std::cerr << "Failed to create icon texture: " << SDL_GetError() << std::endl;
        return 1;
    }

    // Set icon to its full size of 1024x768
    state.iconRect = {0, 0, 1024, 768};
    
    // Center the icon on screen
    int windowWidth, windowHeight;
    SDL_GetWindowSize(state.window, &windowWidth, &windowHeight);
    state.iconRect.x = (windowWidth - state.iconRect.w) / 2;
    state.iconRect.y = (windowHeight - state.iconRect.h) / 2;

    // Load main screen texture
    std::string mainScreenPath = "../assets/main_screen.png";
    SDL_Surface* mainScreenSurface = IMG_Load(mainScreenPath.c_str());
    if (!mainScreenSurface) {
        std::cerr << "Failed to load main screen image: " << IMG_GetError() << std::endl;
        return 1;
    }
    state.mainScreenTexture = SDL_CreateTextureFromSurface(state.renderer, mainScreenSurface);
    SDL_FreeSurface(mainScreenSurface);
    if (!state.mainScreenTexture) {
        std::cerr << "Failed to create main screen texture: " << SDL_GetError() << std::endl;
        return 1;
    }

    // Set main screen to full window size
    state.mainScreenRect = {0, 0, windowWidth, windowHeight};



    // Load library screen texture
    std::string libraryScreenPath = "../assets/library.png";
    SDL_Surface* libraryScreenSurface = IMG_Load(libraryScreenPath.c_str());
    if (!libraryScreenSurface) {
        std::cerr << "Failed to load library screen image: " << IMG_GetError() << std::endl;
        return 1;
    }
    state.libraryScreenTexture = SDL_CreateTextureFromSurface(state.renderer, libraryScreenSurface);
    SDL_FreeSurface(libraryScreenSurface);
    if (!state.libraryScreenTexture) {
        std::cerr << "Failed to create library screen texture: " << SDL_GetError() << std::endl;
        return 1;
    }

    // Set library screen to full window size
    state.libraryScreenRect = {0, 0, windowWidth, windowHeight};

    // Load attic texture
    std::string atticPath = "../assets/attic.png";
    SDL_Surface* atticSurface = IMG_Load(atticPath.c_str());
    if (!atticSurface) {
        std::cerr << "Failed to load attic image: " << IMG_GetError() << std::endl;
        return 1;
    }
    state.atticTexture = SDL_CreateTextureFromSurface(state.renderer, atticSurface);
    SDL_FreeSurface(atticSurface);
    if (!state.atticTexture) {
        std::cerr << "Failed to create attic texture: " << SDL_GetError() << std::endl;
        return 1;
    }

    // Set attic to full window size
    state.atticRect = {0, 0, windowWidth, windowHeight};

    // Load kitchen texture
    std::string kitchenPath = "../assets/kitchen.png";
    SDL_Surface* kitchenSurface = IMG_Load(kitchenPath.c_str());
    if (!kitchenSurface) {
        std::cerr << "Failed to load kitchen image: " << IMG_GetError() << std::endl;
        return 1;
    }
    state.kitchenTexture = SDL_CreateTextureFromSurface(state.renderer, kitchenSurface);
    SDL_FreeSurface(kitchenSurface);
    if (!state.kitchenTexture) {
        std::cerr << "Failed to create kitchen texture: " << SDL_GetError() << std::endl;
        return 1;
    }

    // Set kitchen to full window size
    state.kitchenRect = {0, 0, windowWidth, windowHeight};

    // Initialize blinking sprite
    state.blinkingSprite.init(state.renderer);

    // Initialize kitchen
    kitchen.init(state.renderer);

    // Load clock hand textures
    SDL_Surface* hourHandSurface = IMG_Load("../assets/hour_hand_nobg.png");
    SDL_Surface* minuteHandSurface = IMG_Load("../assets/minute_hand_nobg.png");
    if (!hourHandSurface || !minuteHandSurface) {
        std::cerr << "Failed to load clock hand images!" << std::endl;
        return 1;
    }

    SDL_Texture* hourHandTexture = SDL_CreateTextureFromSurface(state.renderer, hourHandSurface);
    SDL_Texture* minuteHandTexture = SDL_CreateTextureFromSurface(state.renderer, minuteHandSurface);
    SDL_FreeSurface(hourHandSurface);
    SDL_FreeSurface(minuteHandSurface);

    if (!hourHandTexture || !minuteHandTexture) {
        std::cerr << "Failed to create clock hand textures!" << std::endl;
        return 1;
    }

    // Set clock hand textures in kitchen
    kitchen.setClockHandTextures(hourHandTexture, minuteHandTexture);

    // Set initial game state
    state.showingIcon = true;
    state.showingMainScreen = false;
    state.showingLibraryScreen = false;
    state.showingGameScreen = false;
    state.showingAttic = false;
    state.showingKitchen = false;
    state.iconHovered = false;
    
    if (!loadSentences()) {
        std::cout << "Failed to load sentences from database!" << std::endl;
        return 1;
    }
    if (!loadTimeQuestions()) {
        std::cout << "Failed to load time questions from database!" << std::endl;
        return 1;
    }

    state.timeCheckButtonTexture = createTextTexture("Revisar", {0, 0, 0, 255});
    if (!state.timeCheckButtonTexture) {
        return 1;
    }

    int checkTextWidth = 0;
    int checkTextHeight = 0;
    SDL_QueryTexture(state.timeCheckButtonTexture, nullptr, nullptr, &checkTextWidth, &checkTextHeight);
    state.timeCheckButtonRect = {
        SCREEN_WIDTH - 180,
        SCREEN_HEIGHT - 92,
        checkTextWidth + 28,
        checkTextHeight + 18
    };
    state.timeCheckTextRect = {
        state.timeCheckButtonRect.x + (state.timeCheckButtonRect.w - checkTextWidth) / 2,
        state.timeCheckButtonRect.y + (state.timeCheckButtonRect.h - checkTextHeight) / 2,
        checkTextWidth,
        checkTextHeight
    };

    state.timeStartTextTexture = createTextTexture("Play", {64, 64, 64, 255});
    if (!state.timeStartTextTexture) {
        return 1;
    }

    int startTextWidth = 0;
    int startTextHeight = 0;
    SDL_QueryTexture(state.timeStartTextTexture, nullptr, nullptr, &startTextWidth, &startTextHeight);
    state.timeStartButtonRect = state.originalButtonRect;
    state.timeStartTextRect = {
        state.timeStartButtonRect.x + (state.timeStartButtonRect.w - startTextWidth) / 2,
        state.timeStartButtonRect.y + (state.timeStartButtonRect.h - startTextHeight) / 2,
        startTextWidth,
        startTextHeight
    };

    // Initialize game state
    state.gameStarted = false;
    state.quit = false;
    state.currentSprite = 0;
    state.lastSpriteChange = 0;
    state.nextGhostSpawnTime = 0;
    state.spriteStartTime = 0;
    state.currentGhostDriftStartTime = 0;
    state.fadeAlpha = 0.0f;
    state.currentGhostDriftVelocityX = 0.0f;
    state.currentGhostDriftVelocityY = 0.0f;
    state.fadingIn = false;
    state.waitingToFadeOut = false;
    state.showingFeedback = false;
    state.answerFeedback = false;
    state.feedbackStartTime = 0;
    state.feedbackColor = {0, 255, 0, 255};
    state.timePromptTexture = nullptr;
    state.timeDebugTexture = nullptr;
    state.timeGameStarted = false;
    state.showingTimeFeedback = false;
    state.timeAnswerCorrect = false;
    state.timeFeedbackStartTime = 0;
    state.currentTimeQuestion = -1;
    // Select initial random sentence
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, state.sentences.size() - 1);
    state.currentSentence = dis(gen);
    updateSentenceDisplay();

    state.currentTargetWordIndex = 0;
    state.currentTargetWordIndex = 0;

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(mainLoop, 0, 1);
#else
    while (!state.quit) {
        mainLoop();
        SDL_Delay(16);
    }
#endif

    SDL_DestroyTexture(state.backgroundTexture);
    SDL_DestroyTexture(state.buttonTexture);
    SDL_DestroyTexture(state.textTexture);
    SDL_DestroyTexture(state.spanishTextTexture);
    SDL_DestroyTexture(state.englishTextTexture);
    SDL_DestroyTexture(state.targetWordTexture);
    SDL_DestroyTexture(state.timePromptTexture);
    SDL_DestroyTexture(state.timeCheckButtonTexture);
    SDL_DestroyTexture(state.timeDebugTexture);
    SDL_DestroyTexture(state.timeStartTextTexture);
    for (SDL_Texture* sprite : state.sprites) {
        SDL_DestroyTexture(sprite);
    }
    Mix_FreeChunk(state.correctSound);
    Mix_FreeChunk(state.incorrectSound);
    Mix_FreeChunk(state.tombstoneSound);
    Mix_FreeMusic(state.backgroundMusic);
    Mix_CloseAudio();
    TTF_CloseFont(state.font);
    TTF_CloseFont(state.sentenceFont);
    SDL_DestroyRenderer(state.renderer);
    SDL_DestroyWindow(state.window);
    TTF_Quit();
    Mix_Quit();
    IMG_Quit();
    SDL_Quit();

    if (state.playAgainTexture) {
        SDL_DestroyTexture(state.playAgainTexture);
        state.playAgainTexture = nullptr;
    }
    if (state.playAgainTextTexture) {
        SDL_DestroyTexture(state.playAgainTextTexture);
        state.playAgainTextTexture = nullptr;
    }
    if (state.iconTexture) {
        SDL_DestroyTexture(state.iconTexture);
        state.iconTexture = nullptr;
    }
    if (state.mainScreenTexture) {
        SDL_DestroyTexture(state.mainScreenTexture);
        state.mainScreenTexture = nullptr;
    }
    if (state.libraryScreenTexture) {
        SDL_DestroyTexture(state.libraryScreenTexture);
        state.libraryScreenTexture = nullptr;
    }
    if (state.atticTexture) {
        SDL_DestroyTexture(state.atticTexture);
        state.atticTexture = nullptr;
    }
    if (state.kitchenTexture) {
        SDL_DestroyTexture(state.kitchenTexture);
        state.kitchenTexture = nullptr;
    }
    state.blinkingSprite.cleanup();

    return 0;
}

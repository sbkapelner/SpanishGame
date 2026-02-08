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
#include <functional>
#include <cctype>
#include <sqlite3.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "library_game.h"
#include "kitchen/kitchen.h"

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
    SDL_Rect currentSpriteRect;
    SDL_Rect buttonRect;
    SDL_Rect textRect;
    SDL_Rect spanishTextRect;
    SDL_Rect englishTextRect;
    SDL_Rect targetWordRect;
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
    Uint32 spriteStartTime;
    float fadeAlpha;
    bool fadingIn;
    bool waitingToFadeOut;
    std::vector<SentencePair> sentences;
    std::vector<TargetWord> target_words;
    std::vector<int> current_word_choices;
    int currentSentence;
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

void updateSprite() {
    const Uint32 currentTime = SDL_GetTicks();
    const Uint32 spriteDisplayTime = 3000;  // 3 seconds
    const float fadeSpeed = 0.1f;
    
    if (!state.gameStarted) {
        return;
    }

    if (state.fadingIn) {
        state.fadeAlpha += fadeSpeed;
        if (state.fadeAlpha >= 1.0f) {
            state.fadeAlpha = 1.0f;
            state.fadingIn = false;
            state.waitingToFadeOut = true;
            state.spriteStartTime = currentTime;  // Start the display timer
        }
    } else if (state.waitingToFadeOut) {
        // Check if we've displayed for 3 seconds
        if (currentTime - state.spriteStartTime >= spriteDisplayTime) {
            state.waitingToFadeOut = false;  // Start fading out
        }
    } else {
        state.fadeAlpha -= fadeSpeed;
        if (state.fadeAlpha <= 0.0f) {
            state.fadeAlpha = 0.0f;
            
            // Select new random sprite and position
            static std::random_device rd;
            static std::mt19937 gen(rd());
            static std::uniform_int_distribution<> dis(0, state.sprites.size() - 1);
            
            state.currentSprite = dis(gen);
            SDL_Point newPos = getRandomPosition(
                state.spriteSizes[state.currentSprite].x,
                state.spriteSizes[state.currentSprite].y,
                SCREEN_WIDTH, SCREEN_HEIGHT
            );
            
            // Update sprite position
            state.currentSpriteRect.w = state.spriteSizes[state.currentSprite].x;
            state.currentSpriteRect.h = state.spriteSizes[state.currentSprite].y;
            state.currentSpriteRect.x = newPos.x;
            state.currentSpriteRect.y = newPos.y;
            
            state.lastSpriteChange = currentTime;
            state.fadingIn = true;
            
            // Cycle to the next word when sprite changes
            cycleTargetWord();
        }
    }
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
    
    // Find the index of the target word after shuffling
    auto it = std::find(state.current_word_choices.begin(), state.current_word_choices.end(), target_word_id);
    if (it == state.current_word_choices.end()) {
        std::cout << "Error: Target word not found in choices!" << std::endl;
        return;
    }
    state.currentTargetWordIndex = std::distance(state.current_word_choices.begin(), it);
    
    // Print out all word choices for debugging
    std::cout << "Word choices: ";
    for (int id : state.current_word_choices) {
        auto word_it = std::find_if(state.target_words.begin(), state.target_words.end(),
                                  [id](const TargetWord& tw) { return tw.id == id; });
        if (word_it != state.target_words.end()) {
            std::cout << word_it->word << "(" << id << ") ";
        }
    }
    std::cout << "\nCorrect word index: " << state.currentTargetWordIndex << std::endl;
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
                state.lastSpriteChange = SDL_GetTicks();
                state.fadingIn = true;
                state.fadeAlpha = 0.0f;
                state.currentSprite = state.sprites.size(); // Set to invalid sprite index initially
                // Select initial word choices when game starts
                selectNewWordChoices();
                updateTargetWordDisplay();
                updateSentenceDisplay();
            }
            else if (state.gameStarted && !state.showingFeedback) {
                // Check if click is within the current sprite
                if (mouseX >= state.currentSpriteRect.x && 
                    mouseX <= state.currentSpriteRect.x + state.currentSpriteRect.w &&
                    mouseY >= state.currentSpriteRect.y && 
                    mouseY <= state.currentSpriteRect.y + state.currentSpriteRect.h) {
                    
                    // Get the current displayed word's ID from current_word_choices
                    int displayed_word_id = state.current_word_choices[state.currentTargetWordIndex];
                    
                    // Get the target word ID from the current sentence (foreign key)
                    int target_word_id = state.sentences[state.currentSentence].target_word_id;
                    
                    // Find the actual words for debugging
                    auto displayed_word_it = std::find_if(state.target_words.begin(), state.target_words.end(),
                                                        [displayed_word_id](const TargetWord& tw) { return tw.id == displayed_word_id; });
                    auto target_word_it = std::find_if(state.target_words.begin(), state.target_words.end(),
                                                      [target_word_id](const TargetWord& tw) { return tw.id == target_word_id; });
                    
                    std::cout << "\nClick detected on sprite!" << std::endl;
                    std::cout << "Current word index: " << state.currentTargetWordIndex << std::endl;
                    std::cout << "Displayed word: " << (displayed_word_it != state.target_words.end() ? displayed_word_it->word : "unknown") 
                             << " (ID: " << displayed_word_id << ")" << std::endl;
                    std::cout << "Target word: " << (target_word_it != state.target_words.end() ? target_word_it->word : "unknown")
                             << " (ID: " << target_word_id << ")" << std::endl;
                    
                    // Compare the displayed word ID with the target word ID
                    if (displayed_word_id == target_word_id) {
                        std::cout << "Correct answer!" << std::endl;
                        state.answerFeedback = true;
                        state.showingFeedback = true;
                        state.feedbackStartTime = SDL_GetTicks();
                        state.feedbackColor = {0, 255, 0, 255}; // Green for correct
                        state.spriteColor = {255, 255, 200, 255}; // Light yellow
                        Mix_PlayChannel(-1, state.correctSound, 0);
                        
                        // Increment questions answered
                        state.questionsAnswered++;
                        
                        // Check if game should end
                        if (state.questionsAnswered >= 10) {
                            state.gameFinished = true;
                            state.currentSprite = state.sprites.size(); // Hide current sprite
                            
                            // Clear textures for next game
                            if (state.spanishTextTexture) {
                                SDL_DestroyTexture(state.spanishTextTexture);
                                state.spanishTextTexture = nullptr;
                            }
                            if (state.englishTextTexture) {
                                SDL_DestroyTexture(state.englishTextTexture);
                                state.englishTextTexture = nullptr;
                            }
                            if (state.targetWordTexture) {
                                SDL_DestroyTexture(state.targetWordTexture);
                                state.targetWordTexture = nullptr;
                            }
                            
                            // Clear text rectangles
                            state.spanishTextRect = {0, 0, 0, 0};
                            state.englishTextRect = {0, 0, 0, 0};
                            state.targetWordRect = {0, 0, 0, 0};
                        }
                    } else {
                        std::cout << "Incorrect answer!" << std::endl;
                        state.answerFeedback = false;
                        state.showingFeedback = true;
                        state.feedbackStartTime = SDL_GetTicks();
                        state.feedbackColor = {255, 0, 0, 255}; // Red for incorrect
                        state.spriteColor = {255, 200, 200, 255}; // Light red
                        Mix_PlayChannel(-1, state.incorrectSound, 0);
                    }
                }
            }
            else if (state.gameFinished && state.isPlayAgainHovered) {
                // Reset game state
                state.gameFinished = false;
                state.questionsAnswered = 0;
                state.currentSentence = 0;
                state.currentTargetWordIndex = 0;
                state.current_word_choices.clear();
                state.showingFeedback = false;
                state.answerFeedback = false;
                state.transitioningToNextRound = false;
                state.transitionStartTime = 0;
                state.spriteColor = {255, 255, 255, 255};
                state.fadeAlpha = 0.0f;
                state.fadingIn = false;
                state.waitingToFadeOut = false;
                state.currentSprite = state.sprites.size();
                state.lastSpriteChange = SDL_GetTicks();
                state.spriteStartTime = 0;
                state.hoverScale = 1.0f;
                state.isHovered = false;
                state.isPlayAgainHovered = false;
                updateSentenceDisplay();
                selectNewWordChoices();
                updateTargetWordDisplay();
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
                            state.blinkingSprite.setScreen(4); // Back to kitchen position
                        });
                    } else {
                        // Start fade out and transition to main screen when complete
                        state.blinkingSprite.startFadeOut([&]() {
                            std::cout << "Transitioning back to main screen from kitchen..." << std::endl;
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
                        state.blinkingSprite.setScreen(4); // Keep kitchen position for now
                    });
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
}

void resetGameState() {
    state.gameStarted = false;
    state.gameFinished = false;
    state.questionsAnswered = 0;
    state.currentSentence = 0;
    state.currentTargetWordIndex = 0;
    state.showingFeedback = false;
    state.answerFeedback = false;
    state.transitioningToNextRound = false;
    state.currentSprite = state.sprites.size(); // Hide current sprite
    state.fadeAlpha = 0.0f;
    state.fadingIn = false;
    state.waitingToFadeOut = false;
    
    // Clear any existing textures
    if (state.spanishTextTexture) {
        SDL_DestroyTexture(state.spanishTextTexture);
        state.spanishTextTexture = nullptr;
    }
    if (state.englishTextTexture) {
        SDL_DestroyTexture(state.englishTextTexture);
        state.englishTextTexture = nullptr;
    }
    if (state.targetWordTexture) {
        SDL_DestroyTexture(state.targetWordTexture);
        state.targetWordTexture = nullptr;
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
            state.spriteColor = {255, 255, 255, 255}; // Reset to white
            
            // If answer was correct, move to next round
            if (state.answerFeedback) {
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
        updateTargetWordDisplay();
    }
    
    updateButtonAnimation();
    updateSprite();

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
            
            if (state.currentSprite < state.sprites.size()) {
                // Set base opacity to 75% (192) and then apply fade alpha
                Uint8 baseOpacity = 192;  // 75% opacity
                SDL_SetTextureAlphaMod(state.sprites[state.currentSprite], 
                                     static_cast<Uint8>(state.fadeAlpha * baseOpacity));
                SDL_SetTextureColorMod(state.sprites[state.currentSprite],
                                      state.spriteColor.r,
                                      state.spriteColor.g,
                                      state.spriteColor.b);
                SDL_RenderCopy(state.renderer, state.sprites[state.currentSprite], 
                              NULL, &state.currentSpriteRect);
                
                if (state.targetWordTexture && state.gameStarted) {
                    SDL_SetTextureAlphaMod(state.targetWordTexture, 
                                         static_cast<Uint8>(state.fadeAlpha * baseOpacity));
                    SDL_RenderCopy(state.renderer, state.targetWordTexture, 
                                  NULL, &state.targetWordRect);
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
    kitchen.setClockHandTextures(minuteHandTexture, hourHandTexture);

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

    // Initialize game state
    state.gameStarted = false;
    state.quit = false;
    state.currentSprite = 0;
    state.lastSpriteChange = 0;
    state.spriteStartTime = 0;
    state.fadeAlpha = 0.0f;
    state.fadingIn = false;
    state.waitingToFadeOut = false;
    state.showingFeedback = false;
    state.answerFeedback = false;
    state.feedbackStartTime = 0;
    state.feedbackColor = {0, 255, 0, 255};
    
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

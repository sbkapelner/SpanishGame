#include "library_game.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <fstream>
#include <sstream>
#include <iostream>

// Global variables
extern std::vector<BookSprite> bookSprites;
extern bool showingBookPage;
extern SDL_Texture* bookPageTexture;
extern SDL_Rect libraryBackButtonRect;
extern SDL_Rect bookPageBackButtonRect;
extern std::vector<std::string> currentBookWords;
extern size_t currentWordIndex;
extern size_t lastDisplayedWord;
extern Uint32 lastWordTime;
extern bool showNextArrow;
extern SDL_Rect nextArrowRect;
extern bool isHoveringNextArrow;

void initBackButtons() {
    // Position book page back button slightly higher than library back button
    bookPageBackButtonRect = {20, SCREEN_HEIGHT - 150, 200, 100}; // Book page back button
    libraryBackButtonRect = {20, SCREEN_HEIGHT - 100, 200, 100}; // Library back button
    
    // Initialize next arrow position
    const int arrowSize = 40;
    nextArrowRect = {
        SCREEN_WIDTH - 80,  // X position
        SCREEN_HEIGHT - 80, // Y position
        arrowSize,         // Width
        arrowSize          // Height
    };
}

void loadBookContent(const std::string& contentPath) {
    currentBookWords.clear();
    currentWordIndex = 1;      // Start with first word
    lastDisplayedWord = 0;     // Start from beginning
    showNextArrow = false;     // No arrow at start
    lastWordTime = SDL_GetTicks();
    
    std::cout << "Opening book file: " << contentPath << std::endl;
    std::ifstream file(contentPath);
    if (!file.is_open()) {
        std::cerr << "Failed to open book content file: " << contentPath << std::endl;
        return;
    }
    
    // Store words in a temporary vector first
    std::vector<std::string> allWords;
    std::string line;
    
    // Read line by line
    while (std::getline(file, line)) {
        if (line.empty()) {
            // Empty line means newline
            allWords.push_back("\n");
            continue;
        }
        
        // Split line into words
        std::stringstream ss(line);
        std::string word;
        while (ss >> word) {
            allWords.push_back(word);
        }
    }
    
    file.close();
    
    // Now store all words
    currentBookWords = allWords;
    std::cout << "Loaded " << currentBookWords.size() << " words from book" << std::endl;
}

bool isMouseOverNextArrow(int mouseX, int mouseY) {
    return mouseX >= nextArrowRect.x && mouseX < nextArrowRect.x + nextArrowRect.w &&
           mouseY >= nextArrowRect.y && mouseY < nextArrowRect.y + nextArrowRect.h;
}

void handleNextArrowClick() {
    if (showNextArrow) {
        showNextArrow = false;     // Hide arrow
        lastDisplayedWord += currentWordIndex;  // Move to next set of words
        currentWordIndex = 1;      // Start with first word of new page
        lastWordTime = SDL_GetTicks();  // Reset animation timer
    }
}

void renderBookSprites(SDL_Renderer* renderer) {
    if (showingBookPage) {
        renderBookPage(renderer);
        return;
    }

    // Initialize book sprites if not already done
    if (bookSprites.empty()) {
        const int SPRITE_COUNT = 6;
        const int SPRITE_SIZE = 200; // Set the size of each sprite
        const int GRID_COLUMNS = 3; // Number of columns in the grid
        const int GRID_ROWS = (SPRITE_COUNT + GRID_COLUMNS - 1) / GRID_COLUMNS; // Calculate rows needed

        const char* spritePaths[SPRITE_COUNT] = {
            "../assets/book-blue.png",
            "../assets/book-brown.png",
            "../assets/book-dark-green.png",
            "../assets/book-green.png",
            "../assets/book-purple.png",
            "../assets/book-teal.png"
        };

        // Calculate the total width and height of the grid
        int totalWidth = GRID_COLUMNS * SPRITE_SIZE;
        int totalHeight = GRID_ROWS * SPRITE_SIZE;

        // Calculate starting position to center the grid
        int startX = (SCREEN_WIDTH - totalWidth) / 2;
        int startY = (SCREEN_HEIGHT - totalHeight) / 2;

        for (int i = 0; i < SPRITE_COUNT; ++i) {
            BookSprite sprite;
            sprite.texture = loadTexture(spritePaths[i], renderer);
            sprite.contentPath = std::string("../assets/books/book") + std::to_string(i+1) + ".txt";
            
            // Calculate grid position
            int row = i / GRID_COLUMNS;
            int col = i % GRID_COLUMNS;
            
            sprite.rect.x = startX + col * SPRITE_SIZE;
            sprite.rect.y = startY + row * SPRITE_SIZE;
            sprite.rect.w = SPRITE_SIZE;
            sprite.rect.h = SPRITE_SIZE;
            sprite.scale = 1.0f;
            
            bookSprites.push_back(sprite);
        }

        // Load book page texture
        bookPageTexture = loadTexture("../assets/book_page.png", renderer);
    }

    // Render all book sprites
    for (const auto& sprite : bookSprites) {
        // Get current mouse position
        int mouseX, mouseY;
        SDL_GetMouseState(&mouseX, &mouseY);
        
        // Check if mouse is over this sprite
        bool isHovered = mouseX >= sprite.rect.x && mouseX < sprite.rect.x + sprite.rect.w &&
                        mouseY >= sprite.rect.y && mouseY < sprite.rect.y + sprite.rect.h;
        
        // Calculate scaled dimensions
        float currentScale = isHovered ? sprite.scale * 1.1f : sprite.scale;
        int scaledW = static_cast<int>(sprite.rect.w * currentScale);
        int scaledH = static_cast<int>(sprite.rect.h * currentScale);
        
        // Center the scaled sprite on its original position
        int scaledX = sprite.rect.x + (sprite.rect.w - scaledW) / 2;
        int scaledY = sprite.rect.y + (sprite.rect.h - scaledH) / 2;
        
        SDL_Rect renderRect = {scaledX, scaledY, scaledW, scaledH};
        SDL_RenderCopy(renderer, sprite.texture, nullptr, &renderRect);
    }

    // Only render library back button when not showing book page
    if (!showingBookPage) {
        int mouseX, mouseY;
        SDL_GetMouseState(&mouseX, &mouseY);
        if (isMouseOverLibraryBack(mouseX, mouseY)) {
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 64);
            SDL_RenderFillRect(renderer, &libraryBackButtonRect);
        }
    }
}

void renderBookPage(SDL_Renderer* renderer) {
    if (!bookPageTexture) {
        SDL_Log("No book page texture!");
        return;
    }
    
    // Draw the book page background
    SDL_RenderCopy(renderer, bookPageTexture, nullptr, nullptr);
    
    // Draw back button
    SDL_SetRenderDrawColor(renderer, 200, 200, 200, 64);
    SDL_RenderFillRect(renderer, &bookPageBackButtonRect);
    
    // Highlight back button if hovered
    int mouseX, mouseY;
    SDL_GetMouseState(&mouseX, &mouseY);
    if (isMouseOverBookPageBack(mouseX, mouseY)) {
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 64);
        SDL_RenderFillRect(renderer, &bookPageBackButtonRect);
    }
    
    // Column layout parameters
    const int screenCenter = SCREEN_WIDTH / 2;
    const int marginFromEdge = 50;  // Space from screen edges
    const int columnWidth = (screenCenter - marginFromEdge * 2);  // Width for each column
    const int leftColumnX = marginFromEdge;  // Left column starts from left margin
    const int rightColumnX = screenCenter + marginFromEdge;  // Right column starts after center + margin
    const int maxColumnHeight = SCREEN_HEIGHT - 100;  // Maximum height for both columns
    
    // Render text
    TTF_Font* font = TTF_OpenFont("../assets/Arial.ttf", 24);
    if (!font) {
        SDL_Log("Failed to load font! SDL_ttf Error: %s", TTF_GetError());
        return;
    }

    SDL_Color textColor = {0, 0, 0, 255};  // Black text
    
    // Calculate word positions
    std::vector<SDL_Point> wordPositions;  // Store positions for each word
    std::vector<SDL_Surface*> wordSurfaces;  // Store surfaces for reuse
    
    int currentX = leftColumnX;
    int currentY = 50;
    int currentLineHeight = 0;
    bool inFirstColumn = true;
    bool pageFull = false;
    
    // First pass: Calculate positions and check page limits
    for (size_t i = lastDisplayedWord; i < lastDisplayedWord + currentWordIndex && i < currentBookWords.size(); ++i) {
        // Handle newline character
        if (currentBookWords[i] == "\n") {
            currentY += currentLineHeight + 20;  // Increased spacing for newlines
            currentX = inFirstColumn ? leftColumnX : rightColumnX;
            currentLineHeight = 0;
            continue;
        }

        // Use UTF8 rendering
        SDL_Surface* surface = TTF_RenderUTF8_Blended(font, currentBookWords[i].c_str(), textColor);
        if (!surface) continue;
        
        // Check if word would exceed column width
        if (currentX + surface->w > (inFirstColumn ? leftColumnX + columnWidth : rightColumnX + columnWidth)) {
            currentY += currentLineHeight + 10;  // Move to next line
            currentX = inFirstColumn ? leftColumnX : rightColumnX;
            currentLineHeight = 0;
        }
        
        // Check if exceeding column height
        if (currentY + surface->h > maxColumnHeight) {
            if (inFirstColumn) {
                inFirstColumn = false;
                currentX = rightColumnX;
                currentY = 50;
                currentLineHeight = 0;
            } else {
                // Second column is full
                pageFull = true;
                SDL_FreeSurface(surface);
                break;
            }
        }
        
        SDL_Point pos = {currentX, currentY};
        wordPositions.push_back(pos);
        wordSurfaces.push_back(surface);
        
        currentLineHeight = std::max(currentLineHeight, surface->h);
        currentX += surface->w + 10;  // Add word spacing
    }
    
    // Second pass: Render words
    for (size_t i = 0; i < wordPositions.size(); ++i) {
        SDL_Surface* surface = wordSurfaces[i];
        SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
        if (!texture) continue;
        
        SDL_Rect destRect = {
            wordPositions[i].x,
            wordPositions[i].y,
            surface->w,
            surface->h
        };
        
        SDL_RenderCopy(renderer, texture, nullptr, &destRect);
        SDL_DestroyTexture(texture);
    }
    
    // Cleanup surfaces
    for (auto surface : wordSurfaces) {
        SDL_FreeSurface(surface);
    }
    
    TTF_CloseFont(font);
    
    // Update page state
    if (pageFull && !showNextArrow) {
        showNextArrow = true;  // Show arrow when page is full
    } else if (lastDisplayedWord + currentWordIndex >= currentBookWords.size()) {
        showNextArrow = false;  // No more words to show
    }
    
    // Draw next arrow if needed
    if (showNextArrow) {
        SDL_SetRenderDrawColor(renderer, isHoveringNextArrow ? 128 : 0, 0, 0, 255);
        
        // Draw thicker lines for better visibility
        for (int offset = -1; offset <= 1; offset++) {
            // Horizontal line
            SDL_RenderDrawLine(renderer, 
                nextArrowRect.x, nextArrowRect.y + nextArrowRect.h/2 + offset,
                nextArrowRect.x + nextArrowRect.w, nextArrowRect.y + nextArrowRect.h/2 + offset);
            
            // Upper diagonal
            SDL_RenderDrawLine(renderer,
                nextArrowRect.x + nextArrowRect.w - offset, nextArrowRect.y + nextArrowRect.h/2,
                nextArrowRect.x + nextArrowRect.w/2, nextArrowRect.y + offset);
            
            // Lower diagonal
            SDL_RenderDrawLine(renderer,
                nextArrowRect.x + nextArrowRect.w - offset, nextArrowRect.y + nextArrowRect.h/2,
                nextArrowRect.x + nextArrowRect.w/2, nextArrowRect.y + nextArrowRect.h + offset);
        }
    }
}

void handleBookClick(int mouseX, int mouseY) {
    if (!showingBookPage) {
        for (const auto& sprite : bookSprites) {
            if (mouseX >= sprite.rect.x && mouseX < sprite.rect.x + sprite.rect.w &&
                mouseY >= sprite.rect.y && mouseY < sprite.rect.y + sprite.rect.h) {
                std::cout << "Loading book content from: " << sprite.contentPath << std::endl;
                loadBookContent(sprite.contentPath);
                std::cout << "Loaded " << currentBookWords.size() << " words" << std::endl;
                showingBookPage = true;
                break;
            }
        }
    }
}

bool isMouseOverLibraryBack(int mouseX, int mouseY) {
    // Only check library back button when not showing book page
    if (showingBookPage) {
        return false;
    }
    return mouseX >= libraryBackButtonRect.x && mouseX < libraryBackButtonRect.x + libraryBackButtonRect.w &&
           mouseY >= libraryBackButtonRect.y && mouseY < libraryBackButtonRect.y + libraryBackButtonRect.h;
}

bool isMouseOverBookPageBack(int mouseX, int mouseY) {
    // Only check book page back button when showing book page
    if (!showingBookPage) {
        return false;
    }
    return mouseX >= bookPageBackButtonRect.x && mouseX < bookPageBackButtonRect.x + bookPageBackButtonRect.w &&
           mouseY >= bookPageBackButtonRect.y && mouseY < bookPageBackButtonRect.y + bookPageBackButtonRect.h;
}

void cleanupLibraryTextures() {
    for (auto& sprite : bookSprites) {
        SDL_DestroyTexture(sprite.texture);
    }
    bookSprites.clear();
    
    if (bookPageTexture) {
        SDL_DestroyTexture(bookPageTexture);
        bookPageTexture = nullptr;
    }
}

void updateTextAnimation() {
    Uint32 currentTime = SDL_GetTicks();
    if (currentTime - lastWordTime >= 500) {  // 500ms between words
        if (!showNextArrow && lastDisplayedWord + currentWordIndex < currentBookWords.size()) {
            currentWordIndex++;
            lastWordTime = currentTime;
        }
    }
}
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Calculator state
typedef struct {
  char display[32];
  double storedValue;
  char pendingOp;
  int hasPendingOp;
  int clearOnNextDigit;
} Calculator;

// Button definition
typedef struct {
  SDL_Rect rect;
  char label[8];
  SDL_Color color;
} Button;

// Colors
SDL_Color COLOR_BG = {30, 30, 30, 255};
SDL_Color COLOR_DISPLAY = {45, 45, 45, 255};
SDL_Color COLOR_DIGIT = {70, 70, 70, 255};
SDL_Color COLOR_OP = {255, 149, 0, 255};
SDL_Color COLOR_CLEAR = {165, 165, 165, 255};
SDL_Color COLOR_TEXT = {255, 255, 255, 255};

// Global calculator
Calculator calc = {"0", 0, 0, 0, 0};
Button buttons[16];
int numButtons = 0;

void calc_inputDigit(const char *digit) {
  if (strcmp(calc.display, "0") == 0 || calc.clearOnNextDigit) {
    strcpy(calc.display, digit);
    calc.clearOnNextDigit = 0;
  } else if (strlen(calc.display) < 15) {
    strcat(calc.display, digit);
  }
}

void calc_inputOperator(char op) {
  calc.storedValue = atof(calc.display);
  calc.pendingOp = op;
  calc.hasPendingOp = 1;
  strcpy(calc.display, "0");
}

// History
typedef struct {
  char equation[64];
  double result;
} HistoryEntry;

HistoryEntry history[8];
int historyCount = 0;

void addToHistory(const char *opA, char op, const char *opB, double result) {
  // Shift if full
  if (historyCount == 8) {
    for (int i = 0; i < 7; i++) {
      history[i] = history[i + 1];
    }
    historyCount = 7;
  }

  // Add new
  HistoryEntry *entry = &history[historyCount++];
  snprintf(entry->equation, sizeof(entry->equation), "%s %c %s =", opA, op,
           opB);
  entry->result = result;
}

void loadHistory(int index) {
  if (index >= 0 && index < historyCount) {
    snprintf(calc.display, sizeof(calc.display), "%g", history[index].result);
    calc.storedValue = 0;
    calc.hasPendingOp = 0;
    calc.clearOnNextDigit = 1;
  }
}
void calc_inputEquals(void) {
  if (!calc.hasPendingOp)
    return;

  double current = atof(calc.display);
  double result = 0;

  switch (calc.pendingOp) {
  case '+':
    result = calc.storedValue + current;
    break;
  case '-':
    result = calc.storedValue - current;
    break;
  case '*':
    result = calc.storedValue * current;
    break;
  case '/':
    result = (current == 0) ? 0 : calc.storedValue / current;
    break;
  }

  // Add to history
  char opA[32];
  snprintf(opA, sizeof(opA), "%g", calc.storedValue);
  addToHistory(opA, calc.pendingOp, calc.display, result);

  snprintf(calc.display, sizeof(calc.display), "%g", result);
  calc.hasPendingOp = 0;
  calc.clearOnNextDigit = 1;
}

void calc_inputClear(void) {
  strcpy(calc.display, "0");
  calc.storedValue = 0;
  calc.pendingOp = 0;
  calc.hasPendingOp = 0;
  calc.clearOnNextDigit = 0;
}

void calc_inputBackspace(void) {
  int len = strlen(calc.display);
  if (len > 1) {
    calc.display[len - 1] = '\0';
  } else {
    strcpy(calc.display, "0");
  }
}

// Helper to format number with commas
void formatNumber(const char *src, char *dest, size_t destSize) {
  // Find decimal point
  const char *dot = strchr(src, '.');
  int integerLen = dot ? (int)(dot - src) : (int)strlen(src);
  int isNegative = (src[0] == '-');

  int srcIdx = integerLen - 1;
  int destIdx = 0;
  int digitCount = 0;

  // Space for null terminator
  char temp[64];
  int tempIdx = 0;

  // Handle fractional part
  if (dot) {
    const char *frac = dot;
    while (*frac && tempIdx < 60) {
      temp[tempIdx++] = *frac;
      frac++;
    }
  }

  // Handle integer part in reverse
  while (srcIdx >= (isNegative ? 1 : 0)) {
    if (digitCount > 0 && digitCount % 3 == 0) {
      temp[tempIdx++] = ',';
    }
    temp[tempIdx++] = src[srcIdx--];
    digitCount++;
  }

  if (isNegative) {
    temp[tempIdx++] = '-';
  }

  // Reverse temp into dest
  int i;
  for (i = 0; i < tempIdx && i < destSize - 1; i++) {
    dest[i] = temp[tempIdx - 1 - i];
  }
  dest[i] = '\0';
}

// 0-9, +, -, *, /, =, C, O, V, R, F, L, W, H, I, S, T
static const unsigned char patterns[26][5] = {
    {0b111, 0b101, 0b101, 0b101, 0b111}, // 0
    {0b010, 0b110, 0b010, 0b010, 0b111}, // 1
    {0b111, 0b001, 0b111, 0b100, 0b111}, // 2
    {0b111, 0b001, 0b111, 0b001, 0b111}, // 3
    {0b101, 0b101, 0b111, 0b001, 0b001}, // 4
    {0b111, 0b100, 0b111, 0b001, 0b111}, // 5
    {0b111, 0b100, 0b111, 0b101, 0b111}, // 6
    {0b111, 0b001, 0b001, 0b001, 0b001}, // 7
    {0b111, 0b101, 0b111, 0b101, 0b111}, // 8
    {0b111, 0b101, 0b111, 0b001, 0b111}, // 9
    {0b010, 0b010, 0b111, 0b010, 0b010}, // + (10)
    {0b000, 0b000, 0b111, 0b000, 0b000}, // - (11)
    {0b101, 0b010, 0b101, 0b010, 0b101}, // * (12)
    {0b001, 0b001, 0b010, 0b100, 0b100}, // / (13)
    {0b000, 0b111, 0b000, 0b111, 0b000}, // = (14)
    {0b111, 0b100, 0b100, 0b100, 0b111}, // C (15)
    {0b111, 0b101, 0b101, 0b101, 0b111}, // O (16)
    {0b101, 0b101, 0b101, 0b101, 0b010}, // V (17)
    {0b110, 0b101, 0b110, 0b101, 0b101}, // R (18)
    {0b111, 0b100, 0b110, 0b100, 0b100}, // F (19)
    {0b100, 0b100, 0b100, 0b100, 0b111}, // L (20)
    {0b101, 0b101, 0b101, 0b111, 0b101}, // W (21)
    {0b101, 0b101, 0b111, 0b101, 0b101}, // H (22)
    {0b111, 0b010, 0b010, 0b010, 0b111}, // I (23)
    {0b111, 0b100, 0b111, 0b001, 0b111}, // S (24)
    {0b111, 0b010, 0b010, 0b010, 0b010}, // T (25)
};

void drawDigit(SDL_Renderer *ren, int digit, int x, int y, int scale) {
  if (digit < 0 || digit > 25)
    return;

  for (int row = 0; row < 5; row++) {
    for (int col = 0; col < 3; col++) {
      if (patterns[digit][row] & (1 << (2 - col))) {
        SDL_Rect r = {x + col * scale, y + row * scale, scale - 1, scale - 1};
        SDL_RenderFillRect(ren, &r);
      }
    }
  }
}

int charToPattern(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  switch (c) {
  case '+':
    return 10;
  case '-':
    return 11;
  case '*':
    return 12;
  case '/':
    return 13;
  case '=':
    return 14;
  case 'C':
    return 15;
  case 'O':
    return 16;
  case 'V':
  case 'R':
    return 18;
  case 'F':
    return 19;
  case 'L':
    return 20;
  case 'W':
    return 21;
  case 'H':
    return 22;
  case 'I':
    return 23;
  case 'S':
    return 24;
  case 'T':
    return 25;
  case '.':
    return -2; // special handling
  case ',':
    return -3; // comma handling
  }
  return -1;
}

void drawText(SDL_Renderer *ren, const char *text, int x, int y, int scale) {
  int len = strlen(text);
  int charWidth = 4 * scale;

  for (int i = 0; i < len; i++) {
    int pattern = charToPattern(text[i]);
    if (pattern >= 0) {
      drawDigit(ren, pattern, x + i * charWidth, y, scale);
    } else if (text[i] == '.') {
      SDL_Rect r = {x + i * charWidth + scale, y + 4 * scale, scale - 1,
                    scale - 1};
      SDL_RenderFillRect(ren, &r);
    } else if (text[i] == ',') {
      // Draw comma (small vertical stroke at bottom)
      SDL_Rect r1 = {x + i * charWidth + scale, y + 4 * scale, scale - 1,
                     scale - 1};
      SDL_RenderFillRect(ren, &r1);
      // Tail
      SDL_Rect r2 = {x + i * charWidth + scale - 1, y + 5 * scale, scale / 2,
                     scale / 2};
      SDL_RenderFillRect(ren, &r2);
    }
  }
}

// Global variables for history toggle
int showHistory = 0;
Button histBtn;

void initButtons(void) {
  int bw = 60, bh = 50, gap = 10;
  int startX = 20, startY = 120; // Moved down from 90

  // Digit buttons 1-9
  int nums[3][3] = {{7, 8, 9}, {4, 5, 6}, {1, 2, 3}};
  for (int row = 0; row < 3; row++) {
    for (int col = 0; col < 3; col++) {
      Button *b = &buttons[numButtons++];
      b->rect = (SDL_Rect){startX + col * (bw + gap), startY + row * (bh + gap),
                           bw, bh};
      snprintf(b->label, sizeof(b->label), "%d", nums[row][col]);
      b->color = COLOR_DIGIT;
    }
  }

  // 0 button
  Button *b0 = &buttons[numButtons++];
  b0->rect = (SDL_Rect){startX, startY + 3 * (bh + gap), bw, bh};
  strcpy(b0->label, "0");
  b0->color = COLOR_DIGIT;

  // = button
  Button *beq = &buttons[numButtons++];
  beq->rect = (SDL_Rect){startX + (bw + gap), startY + 3 * (bh + gap), bw, bh};
  strcpy(beq->label, "=");
  beq->color = COLOR_OP;

  // C button
  Button *bc = &buttons[numButtons++];
  bc->rect =
      (SDL_Rect){startX + 2 * (bw + gap), startY + 3 * (bh + gap), bw, bh};
  strcpy(bc->label, "C");
  bc->color = COLOR_CLEAR;

  // Operator buttons
  char ops[4] = {'+', '-', '*', '/'};
  for (int i = 0; i < 4; i++) {
    Button *bop = &buttons[numButtons++];
    bop->rect =
        (SDL_Rect){startX + 3 * (bw + gap), startY + i * (bh + gap), bw, bh};
    snprintf(bop->label, sizeof(bop->label), "%c", ops[i]);
    bop->color = COLOR_OP;
  }

  // History Toggle Button
  histBtn.rect = (SDL_Rect){20, 80, 60, 30}; // Left-aligned
  strcpy(histBtn.label, "HIST");
  histBtn.color = COLOR_OP;
}

// Global window pointer for resizing
static SDL_Window *gWindow = NULL;

void handleButtonClick(int x, int y){
  SDL_Window *win = gWindow;
  //History button
  if (x >= histBtn.rect.x && x < histBtn.rect.x + histBtn.rect.w && y >= histBtn.rect.y && y < histBtn.rect.h){
    showHistory = !showHistory;
    SDL_SetWindowSize(win, showHistory ? 500 : 300, 390);
    return;
  }

  //History sidebar
  if (showHistory && x > 300){
    int startY = 20;
    int itemH = 40;
    int index = (y = startY)/ itemH;
    if(index >= 0 && index < historyCount){
      loadHistory(index);
      return;
    }
  }


  // Calculator buttons
  for (int i = 0; i < numButtons; i++) {
    SDL_Rect *r = &buttons[i].rect;
    if (x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h) {
      char *label = buttons[i].label;
      if (label[0] >= '0' && label[0] <= '9') {
        calc_inputDigit(label);
      } else if (label[0] == '+' || label[0] == '-' || label[0] == '*' ||
                 label[0] == '/') {
        calc_inputOperator(label[0]);
      } else if (label[0] == '=') {
        calc_inputEquals();
      } else if (label[0] == 'C') {
        calc_inputClear();
      }
      break;
    }
  }
}

void handleKeyboard(SDL_Keycode key) {
  // Digits 0-9
  if (key >= SDLK_0 && key <= SDLK_9) {
    char digit[2] = {(char)key, '\0'};
    calc_inputDigit(digit);
    return;
  }

  // Numpad digits
  if (key >= SDLK_KP_0 && key <= SDLK_KP_9) {
    char digit[2] = {(char)('0' + (key - SDLK_KP_0)), '\0'};
    calc_inputDigit(digit);
    return;
  }

  // Operators
  switch (key) {
  case SDLK_PLUS:
  case SDLK_KP_PLUS:
    calc_inputOperator('+');
    break;
  case SDLK_MINUS:
  case SDLK_KP_MINUS:
    calc_inputOperator('-');
    break;
  case SDLK_ASTERISK:
  case SDLK_KP_MULTIPLY:
    calc_inputOperator('*');
    break;
  case SDLK_SLASH:
  case SDLK_KP_DIVIDE:
    calc_inputOperator('/');
    break;
  case SDLK_EQUALS:
  case SDLK_KP_EQUALS:
  case SDLK_RETURN:
  case SDLK_KP_ENTER:
    calc_inputEquals();
    break;
  case SDLK_c:
  case SDLK_ESCAPE:
    calc_inputClear();
    break;
  case SDLK_BACKSPACE:
    calc_inputBackspace();
    break;
  }
}

void render(SDL_Renderer *ren) {
  // Background
  SDL_SetRenderDrawColor(ren, COLOR_BG.r, COLOR_BG.g, COLOR_BG.b, 255);
  SDL_RenderClear(ren);

  // --- Sidebar (History) ---
  if (showHistory) {
    SDL_Rect sidebar = {300, 0, 200, 390}; // Height 390
    SDL_SetRenderDrawColor(ren, 40, 40, 40, 255);
    SDL_RenderFillRect(ren, &sidebar);

    // Separator line
    SDL_SetRenderDrawColor(ren, 60, 60, 60, 255);
    SDL_RenderDrawLine(ren, 300, 0, 300, 390); // Height 390

    // History Itemsss
    for (int i = 0; i < historyCount; i++) {
      int y = 20 + i * 40;

      // Equation centered/leftish
      SDL_SetRenderDrawColor(ren, 150, 150, 150, 255);
      drawText(ren, history[i].equation, 310, y, 2);

      // Result
      char resStr[32];
      snprintf(resStr, sizeof(resStr), "%g", history[i].result);
      // Format if possible?
      char fmtRes[64];
      formatNumber(resStr, fmtRes, sizeof(fmtRes));

      SDL_SetRenderDrawColor(ren, COLOR_TEXT.r, COLOR_TEXT.g, COLOR_TEXT.b,
                             255);
      drawText(ren, fmtRes, 310, y + 15, 3);
    }
  }

  // --- Calculator ---

  // Display background
  SDL_Rect displayRect = {20, 20, 260, 50};
  SDL_SetRenderDrawColor(ren, COLOR_DISPLAY.r, COLOR_DISPLAY.g, COLOR_DISPLAY.b,
                         255);
  SDL_RenderFillRect(ren, &displayRect);

  // Format the number
  char formattedText[64];
  formatNumber(calc.display, formattedText, sizeof(formattedText));

  // Check for overflow (max width)
  int scale = 4;
  int charWidth = 4 * scale;
  int maxDisplayWidth = displayRect.w - 20;

  int textWidth = strlen(formattedText) * charWidth;
  const char *finalText = formattedText;

  if (textWidth > maxDisplayWidth) {
    finalText = "OVRFLOW";
    textWidth = strlen(finalText) * charWidth;
  }

  // Display text (right-aligned)
  SDL_SetRenderDrawColor(ren, COLOR_TEXT.r, COLOR_TEXT.g, COLOR_TEXT.b, 255);

  // Calculate X position
  int textX = displayRect.x + displayRect.w - textWidth - 10;

  // Draw
  drawText(ren, finalText, textX, displayRect.y + 15, scale);

  // Draw HIST button
  SDL_SetRenderDrawColor(ren, histBtn.color.r, histBtn.color.g, histBtn.color.b,
                         255);
  SDL_RenderFillRect(ren, &histBtn.rect);
  SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
  // Center Label "HIST" (4 chars * 4px width * 2 scale = 32px wide; 5px height
  // * 2 scale = 10px high) Button 60x30. x = start + (60 - 32)/2 = start + 14
  // y = start + (30 - 10)/2 = start + 10
  drawText(ren, "HIST", histBtn.rect.x + 14, histBtn.rect.y + 10, 2);

  // Buttons
  for (int i = 0; i < numButtons; i++) {
    Button *b = &buttons[i];

    // Button background
    SDL_SetRenderDrawColor(ren, b->color.r, b->color.g, b->color.b, 255);
    SDL_RenderFillRect(ren, &b->rect);

    // Button label (centered)
    SDL_SetRenderDrawColor(ren, COLOR_TEXT.r, COLOR_TEXT.g, COLOR_TEXT.b, 255);
    int labelLen = strlen(b->label);
    int lx = b->rect.x + (b->rect.w - labelLen * 12) / 2;
    int ly = b->rect.y + (b->rect.h - 15) / 2;
    drawText(ren, b->label, lx, ly, 3);
  }

  SDL_RenderPresent(ren);
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;

  SDL_Init(SDL_INIT_VIDEO);

  SDL_Window *win =
      SDL_CreateWindow("jai's calculator", SDL_WINDOWPOS_CENTERED,
                       SDL_WINDOWPOS_CENTERED, 300, 390, SDL_WINDOW_RESIZABLE);
  // Initial height 390
  SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);

  // Keep a reference to resizing isn't fully automatic via SDL_SetWindowSize on
  // some platforms without flags but we'll see. Actually we need to handle the
  // window handle in handleButtonClick properly. SDL_GL_GetCurrentWindow might
  // not work if we didn't create a GL context explicitly? SDL_SetWindowSize
  // needs the window pointer. We can pass 'win' to handleButtonClick or make
  // 'win' global. For simplicity, let's make 'win' global in this small app.
  gWindow = win;

  initButtons();

  int quit = 0;
  SDL_Event e;

  while (!quit) {
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) {
        quit = 1;
      } else if (e.type == SDL_MOUSEBUTTONDOWN) {
        // We need the window to resize it.
        // Let's hack it: define a global `gWindow` and set it in main.
        handleButtonClick(e.button.x, e.button.y);
      } else if (e.type == SDL_KEYDOWN) {
        handleKeyboard(e.key.keysym.sym);
      }
    }

    render(ren);
    SDL_Delay(16); // ~60 FPS
  }

  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  SDL_Quit();
  return 0;
}
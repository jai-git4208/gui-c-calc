#include "model.h"
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
  // RPN Stack (X, Y, Z, T rule, but lets keep it simple: Stack of 4)
  double stack[4];
  int stackSize;
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
Calculator calc = {"0", 0, 0, 0, 0, {0, 0, 0, 0}, 0};
Button buttons[40];
int numButtons = 0;

// Fake Crash State
int divZeroCount = 0;
int isCrashMode = 0;
Uint32 crashStartTime = 0;

// Global window pointer
static SDL_Window *gWindow = NULL;

// Global C Button
Button cBtn;

// Scientific Mode State
typedef enum {
  MODE_BASIC,
  MODE_DRAW,
  MODE_SCIENTIFIC,
  MODE_UNIT,
  MODE_RPN
} CalculatorMode;
CalculatorMode currentMode = MODE_BASIC;
int isDropdownOpen = 0;
Button modeBtn;

// Click Animation State
int clickAnimType =
    -1; // 0=buttons, 1=cBtn, 2=histBtn, 3=drawBtn, 4=drawModeBtns
int clickAnimIdx = -1;
Uint32 clickAnimTime = 0;

void triggerClickAnim(int type, int idx) {
  clickAnimType = type;
  clickAnimIdx = idx;
  clickAnimTime = SDL_GetTicks();
}

void calc_inputEquals(void);

void calc_inputDigit(const char *digit) {
  if (strcmp(digit, ".") == 0) {
    // Check if already has decimal
    if (strchr(calc.display, '.') != NULL) {
      // If we are in "clearOnNextDigit" state, it puts "0.", which is fine.
      // But if we are editing existing "3.14", ignore.
      if (!calc.clearOnNextDigit)
        return;
    }
  }

  if (strcmp(calc.display, "0") == 0 || calc.clearOnNextDigit) {
    if (strcmp(digit, ".") == 0) {
      strcpy(calc.display, "0.");
    } else {
      strcpy(calc.display, digit);
    }
    calc.clearOnNextDigit = 0;
  } else if (strlen(calc.display) < 15) {
    strcat(calc.display, digit);
  }
}

void calc_inputConstant(const char *name) {
  if (strcmp(name, "PI") == 0) {
    snprintf(calc.display, sizeof(calc.display), "%.10g", M_PI);
  } else if (strcmp(name, "e") == 0) {
    snprintf(calc.display, sizeof(calc.display), "%.10g", M_E);
  }
  calc.clearOnNextDigit = 1;
}

void calc_inputUnit(const char *type) {
  double val = atof(calc.display);
  // cm2in: 0.393701
  // in2cm: 2.54
  // kg2lb: 2.20462
  // lb2kg: 0.453592

  if (strcmp(type, "cm2in") == 0)
    val *= 0.393701;
  else if (strcmp(type, "in2cm") == 0)
    val *= 2.54;
  else if (strcmp(type, "kg2lb") == 0)
    val *= 2.20462;
  else if (strcmp(type, "lb2kg") == 0)
    val /= 2.20462;
  else if (strcmp(type, "km2mi") == 0)
    val *= 0.621371;
  else if (strcmp(type, "mi2km") == 0)
    val *= 1.60934;
  else if (strcmp(type, "C2F") == 0)
    val = val * 9.0 / 5.0 + 32.0;
  else if (strcmp(type, "F2C") == 0)
    val = (val - 32.0) * 5.0 / 9.0;

  snprintf(calc.display, sizeof(calc.display), "%.10g", val);
  calc.clearOnNextDigit = 1;
}

// RPN Helpers
void calc_stackPush(double val) {
  // Shift: T=Z, Z=Y, Y=X
  calc.stack[3] = calc.stack[2];
  calc.stack[2] = calc.stack[1];
  calc.stack[1] = calc.stack[0];
  calc.stack[0] = val;
}

double calc_stackPop() {
  double val = calc.stack[0];
  // Shift: X=Y, Y=Z, Z=T
  calc.stack[0] = calc.stack[1];
  calc.stack[1] = calc.stack[2];
  calc.stack[2] = calc.stack[3];
  // T stays same or becomes 0? HP behavior: T duplicates.
  // Let's just keep T or set to 0. 0 is safer for now.
  calc.stack[3] = 0;
  return val;
}

void calc_inputRPN(const char *op) {
  if (strcmp(op, "ENT") == 0) {
    calc_stackPush(atof(calc.display));
    calc.clearOnNextDigit = 1;
  } else if (strcmp(op, "SWP") == 0) {
    double tmp = calc.stack[0];
    calc.stack[0] = calc.stack[1];
    calc.stack[1] = tmp;
    // Update display to match top of stack? In RPN, display is usually X
    // register. So yes, display should become new X.
    snprintf(calc.display, sizeof(calc.display), "%.10g", calc.stack[0]);
  } else if (strcmp(op, "DRP") == 0) {
    calc_stackPop();
    snprintf(calc.display, sizeof(calc.display), "%.10g", calc.stack[0]);
  } else if (strcmp(op, "CLR") == 0) {
    // Clear stack
    for (int i = 0; i < 4; i++)
      calc.stack[i] = 0;
    snprintf(calc.display, sizeof(calc.display), "0");
  }
}

void calc_inputUnary(const char *func) {
  double current = atof(calc.display);
  double result = current;

  if (strcmp(func, "sin") == 0)
    result = sin(current);
  else if (strcmp(func, "cos") == 0)
    result = cos(current);
  else if (strcmp(func, "tan") == 0)
    result = tan(current);
  else if (strcmp(func, "log") == 0)
    result = log10(current);
  else if (strcmp(func, "ln") == 0)
    result = log(current);
  else if (strcmp(func, "sqrt") == 0)
    result = sqrt(current);
  else if (strcmp(func, "sqr") == 0)
    result = current * current;

  snprintf(calc.display, sizeof(calc.display), "%.10g", result);
  calc.clearOnNextDigit = 1;
}

// Basic Operator Handling
void calc_inputOperator(char op) {
  if (currentMode == MODE_RPN) {
    // In RPN, operator acts on stack immediately (X, Y)
    double x = atof(calc.display); // This is conceptually the register, but
                                   // let's sync with stack?
    // Usually X is on stack[0] if we just pushed?
    // If user typed '5' 'Enter' '3', stack has 5. Display has 3.
    // Op '+' -> Pop 5 (Y), take 3 (X). Add. Push Result.

    double y = calc_stackPop(); // Remove Y from stack (it was at 0)

    double res = 0;
    if (op == '+')
      res = y + x;
    else if (op == '-')
      res = y - x;
    else if (op == '*')
      res = y * x;
    else if (op == '/')
      res = (x != 0) ? y / x : 0; // Handle div0 later
    else if (op == '^')
      res = pow(y, x);

    // Push result becomes new X
    calc_stackPush(res);
    snprintf(calc.display, sizeof(calc.display), "%.10g", res);
    calc.clearOnNextDigit = 1;
    return;
  }

  // Basic Mode Logic
  if (calc.hasPendingOp) {
    calc_inputEquals();
  }
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
    if (current == 0) {
      divZeroCount++;
      result = 0; // or infinity
      if (divZeroCount >= 1) {
        isCrashMode = 1;
        crashStartTime = SDL_GetTicks();
        SDL_SetWindowFullscreen(gWindow, SDL_WINDOW_FULLSCREEN_DESKTOP);
      }
    } else {
      divZeroCount = 0; // Reset on valid div
      result = calc.storedValue / current;
    }
    break;
  case '^':
    result = pow(calc.storedValue, current);
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
  // Also clear RPN stack if in RPN mode
  if (currentMode == MODE_RPN) {
    for (int i = 0; i < 4; i++)
      calc.stack[i] = 0;
  }
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
  const char *dot = strchr(src, '.');
  int integerLen = dot ? (int)(dot - src) : (int)strlen(src);
  int isNegative = (src[0] == '-');

  // Temporary buffer for integer part
  char intPart[64];
  int intIdx = 0;

  int srcIdx = integerLen - 1;
  int digitCount = 0;

  // Process integer part (reverse scan)
  while (srcIdx >= (isNegative ? 1 : 0)) {
    if (digitCount > 0 && digitCount % 3 == 0) {
      if (intIdx < 60)
        intPart[intIdx++] = ',';
    }
    if (intIdx < 60)
      intPart[intIdx++] = src[srcIdx--];
    digitCount++;
  }

  if (isNegative) {
    if (intIdx < 60)
      intPart[intIdx++] = '-';
  }

  // Now reverse intPart into dest
  int destIdx = 0;
  for (int i = intIdx - 1; i >= 0 && destIdx < destSize - 1; i--) {
    dest[destIdx++] = intPart[i];
  }

  // Copy fractional part if exists
  if (dot) {
    int i = 0;
    while (dot[i] && destIdx < destSize - 1) {
      dest[destIdx++] = dot[i++];
    }
  }

  dest[destIdx] = '\0';
}

// 0-9, +, -, *, /, =, C, O, V, R, F, L, W, H, I, S, T, A, D, U, E
static const unsigned char patterns[][5] = {
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
    {0b111, 0b101, 0b111, 0b101, 0b101}, // A (26)
    {0b110, 0b101, 0b101, 0b101, 0b110}, // D (27)
    {0b101, 0b101, 0b101, 0b101, 0b111}, // U (28)
    {0b111, 0b100, 0b111, 0b100, 0b111}, // E (29)
    {0b101, 0b111, 0b101, 0b101, 0b101}, // M (30)
    {0b111, 0b101, 0b101, 0b101, 0b101}, // N (31)
    {0b111, 0b101, 0b111, 0b100, 0b100}, // P (32)
    {0b011, 0b100, 0b101, 0b101, 0b111}, // G (33)
    {0b110, 0b101, 0b101, 0b101, 0b110}, // B (34)
    {0b011, 0b101, 0b101, 0b110, 0b001}, // Q (35)
    {0b101, 0b101, 0b010, 0b101, 0b101}, // X (36)
    {0b101, 0b101, 0b010, 0b010, 0b010}, // Y (37)
    {0b101, 0b101, 0b110, 0b101, 0b101}, // K (38)
};

void drawDigit(SDL_Renderer *ren, int digit, int x, int y, int scale) {
  if (digit < 0 || digit > 38)
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
  if (c >= 'a' && c <= 'z')
    c = c - 'a' + 'A'; // To Uppercase

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
  case 'A':
    return 26;
  case 'D':
    return 27;
  case 'U':
    return 28;
  case 'E':
    return 29;
  case 'M':
    return 30;
  case 'N':
    return 31;
  case 'P':
    return 32;
  case 'G':
    return 33;
  case 'B':
    return 34;
  case 'Q':
    return 35;
  case 'X':
    return 36;
  case 'Y':
    return 37;
  case 'K':
    return 38;
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

// Global variables for draw mode
int showDraw = 0;
Button drawBtn;
unsigned char drawGrid[28][28] = {0}; // 28x28 grid, 0=off, 1=on
int isDrawing = 0; // Track if mouse is being dragged for drawing

// ML prediction state
NeuralNetwork nn;
int modelLoaded = 0;
int predictedDigit = -1; // -1 means no prediction yet

// Auto-recognition state
Uint32 lastDrawTime = 0;       // Timestamp of last drawing action
int hasDrawnSomething = 0;     // Flag if grid has any pixels
#define AUTO_PREDICT_DELAY 800 // ms to wait before auto-predicting

// Easter Egg State
Uint32 easterEggStart = 0;
int isHeartAnimActive = 0;

// ML prediction using Neural Network
int predictDigit(void) {
  if (!modelLoaded) {
    printf("Model not loaded, cannot predict.\n");
    return -1;
  }

  // 1. Find bounding box of the drawn digit
  int minR = 28, maxR = -1;
  int minC = 28, maxC = -1;

  for (int r = 0; r < 28; r++) {
    for (int c = 0; c < 28; c++) {
      if (drawGrid[r][c]) {
        if (r < minR)
          minR = r;
        if (r > maxR)
          maxR = r;
        if (c < minC)
          minC = c;
        if (c > maxC)
          maxC = c;
      }
    }
  }

  // Handle empty grid
  if (maxR == -1)
    return -1;

  // 2. Extract and Resize to 20x20 using Bilinear Interpolation
  int w = maxC - minC + 1;
  int h = maxR - minR + 1;

  // Scale factor to fit largest dimension into 20
  float scale = 20.0f / (w > h ? w : h);

  // Target 20x20 buffer
  float scaled20[20][20];
  memset(scaled20, 0, sizeof(scaled20));

  int targetW = (int)(w * scale);
  int targetH = (int)(h * scale);
  // Center of the resized image within the 20x20 box
  int offR = (20 - targetH) / 2;
  int offC = (20 - targetW) / 2;

  for (int r = 0; r < 20; r++) {
    for (int c = 0; c < 20; c++) {
      // If outside the target area, pixel is 0
      if (r < offR || r >= offR + targetH || c < offC || c >= offC + targetW) {
        scaled20[r][c] = 0.0f;
        continue;
      }

      // Map back to Source Coordinate space
      // (r - offR) maps to 0..targetH, which maps to 0..h in source
      float srcR = (r - offR) / scale + minR;
      float srcC = (c - offC) / scale + minC;

      int r0 = (int)srcR;
      int c0 = (int)srcC;

      // Bilinear Interpolation
      if (r0 >= 0 && r0 < 27 && c0 >= 0 && c0 < 27) {
        float dr = srcR - r0;
        float dc = srcC - c0;

        float v00 = drawGrid[r0][c0];
        float v01 = drawGrid[r0][c0 + 1];
        float v10 = drawGrid[r0 + 1][c0];
        float v11 = drawGrid[r0 + 1][c0 + 1];

        scaled20[r][c] = (1 - dr) * (1 - dc) * v00 + (1 - dr) * dc * v01 +
                         dr * (1 - dc) * v10 + dr * dc * v11;
      } else if (r0 >= 0 && r0 < 28 && c0 >= 0 && c0 < 28) {
        // Boundary edge case
        scaled20[r][c] = (float)drawGrid[r0][c0];
      }
    }
  }

  // 3. Center of Mass (CoM) Centering
  float sumMass = 0;
  float sumX = 0;
  float sumY = 0;

  for (int r = 0; r < 20; r++) {
    for (int c = 0; c < 20; c++) {
      sumMass += scaled20[r][c];
      sumX += c * scaled20[r][c];
      sumY += r * scaled20[r][c];
    }
  }

  float comX = 9.5f;
  float comY = 9.5f;
  if (sumMass > 0) {
    comX = sumX / sumMass;
    comY = sumY / sumMass;
  }

  // Calculate offset to shift CoM to center of 28x28 (which is 13.5, 13.5)
  // 20x20 is nominally placed at offset 4,4 in 28x28.
  // We want (comX + 4 + shiftC) = 13.5 => shiftC = 9.5 - comX
  int shiftR = (int)roundf(9.5f - comY);
  int shiftC = (int)roundf(9.5f - comX);

  float processed[28][28] = {0};

  for (int r = 0; r < 20; r++) {
    for (int c = 0; c < 20; c++) {
      int pr = r + 4 + shiftR;
      int pc = c + 4 + shiftC;

      if (pr >= 0 && pr < 28 && pc >= 0 && pc < 28) {
        processed[pr][pc] = scaled20[r][c];
      }
    }
  }

  // Flatten for model
  float input[784];
  for (int i = 0; i < 28; i++) {
    for (int j = 0; j < 28; j++) {
      input[i * 28 + j] = processed[i][j];
    }
  }

  // --- HEART DETECTION ---
  // Simple 28x28 Heart Template (centered approx like the digit)
  // We check for overlap.
  static const int HEART_TEMPLATE[28][28] = {
      {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
       0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
      {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
       0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
      {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
       0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
      {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
       0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
      {0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0,
       0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
      {0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1,
       1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0},
      {0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1,
       1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0},
      {0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
       1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0},
      {0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
       1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0},
      {0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
       1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0},
      {0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
       1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0},
      {0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
       1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0},
      {0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1,
       1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
      {0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1,
       1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
      {0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1,
       1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
      {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1,
       1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
      {0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1,
       0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
      {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0,
       0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
      {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0,
       0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
      {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
       0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
      {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
       0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
      {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
       0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
      {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
       0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
      {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
       0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
      {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
       0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
      {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
       0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
      {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
       0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}};

  float intersection = 0;
  float union_val = 0;

  for (int i = 0; i < 28; i++) {
    for (int j = 0; j < 28; j++) {
      float p = processed[i][j];
      float t = HEART_TEMPLATE[i][j];
      if (p > 0.5 && t > 0.5)
        intersection++;
      if (p > 0.5 || t > 0.5)
        union_val++;
    }
  }

  if (union_val > 0) {
    float iou = intersection / union_val;
    // Threshold for heart detection.
    // User drawings are messy, so 0.45 might be good enough if they try to fill
    // it? Or if it's just outline? The template is filled. If user draws
    // outline, IoU will be low. Let's rely on overlap. If we have high
    // intersection relative to user drawing size? Let's use a simpler metric:
    // Overlap Coefficient = Intersection / min(|A|, |B|) Or just check if
    // enough pixels align. Let's stick to IoU but lenient.
    if (iou > 0.4) {
      return -2; // Start Heart Animation Status
    }
  }

  return model_predict(&nn, input);
}

SDL_Rect displayRect;

void updateLayout(int width, int height) {
  int sidebarWidth = (showHistory || currentMode == MODE_RPN) ? 200 : 0;
  // Calculate Button Dimensions
  // If Scientific/Unit/RPN: 6 cols?
  // RPN: Maybe just 4 cols + Sidebar?
  // Let's make RPN use 6 cols to fit SWP/DRP buttons easily.
  // Standard Calc Width: 300. Sci/Unit/RPN Add 150? -> 450.

  if (currentMode == MODE_SCIENTIFIC || currentMode == MODE_UNIT ||
      currentMode == MODE_RPN) {
    if (width < 450)
      width = 450;
  } else {
    if (width < 300)
      width = 300;
  }

  int controlY = 80;
  int controlH = 30;
  int startY = 120;

  int calcWidth = width - ((showHistory || currentMode == MODE_RPN) ? 200 : 0);
  int padW = calcWidth - 40;

  // Re-calc display
  displayRect = (SDL_Rect){20, 20, calcWidth - 40, 50};

  // Reset all button rects to invisible first
  for (int i = 0; i < numButtons; i++) {
    buttons[i].rect = (SDL_Rect){0, 0, 0, 0};
  }

  cBtn.rect = (SDL_Rect){0, 0, 0, 0};
  histBtn.rect = (SDL_Rect){0, 0, 0, 0};
  drawBtn.rect = (SDL_Rect){0, 0, 0, 0};
  modeBtn.rect = (SDL_Rect){0, 0, 0, 0};

  // Control Row
  // MODE, HIST, C, / (Standard)
  // Sci/Unit/RPN: MODE, HIST, C, /, PI, E (Unit uses Sci layout base)
  int numControl = (currentMode == MODE_SCIENTIFIC ||
                    currentMode == MODE_UNIT || currentMode == MODE_RPN)
                       ? 6
                       : 4;
  int gap = 10;
  int ctrlBtnW = (padW - gap * (numControl - 1)) / numControl;

  modeBtn.rect = (SDL_Rect){20, controlY, ctrlBtnW, controlH};
  histBtn.rect = (SDL_Rect){20 + ctrlBtnW + gap, controlY, ctrlBtnW, controlH};
  cBtn.rect =
      (SDL_Rect){20 + 2 * (ctrlBtnW + gap), controlY, ctrlBtnW, controlH};

  // Update '/'
  for (int i = 0; i < numButtons; i++) {
    if (strcmp(buttons[i].label, "/") == 0)
      buttons[i].rect =
          (SDL_Rect){20 + 3 * (ctrlBtnW + gap), controlY, ctrlBtnW, controlH};
  }

  if (currentMode == MODE_SCIENTIFIC || currentMode == MODE_UNIT ||
      currentMode == MODE_RPN) {
    // PI, E or similar placement
    for (int i = 0; i < numButtons; i++) {
      if (strcmp(buttons[i].label, "PI") == 0)
        buttons[i].rect =
            (SDL_Rect){20 + 4 * (ctrlBtnW + gap), controlY, ctrlBtnW, controlH};
    }
    for (int i = 0; i < numButtons; i++) {
      if (strcmp(buttons[i].label, "e") == 0)
        buttons[i].rect =
            (SDL_Rect){20 + 5 * (ctrlBtnW + gap), controlY, ctrlBtnW, controlH};
    }
  }

  // Keypad
  // Basic: 4 cols. Sci/Unit/RPN: 6 cols.
  int cols = (currentMode == MODE_SCIENTIFIC || currentMode == MODE_UNIT ||
              currentMode == MODE_RPN)
                 ? 6
                 : 4;
  int bw = (padW - gap * (cols - 1)) / cols;

  int padH = height - startY - 20;
  if (padH < 200)
    padH = 200;
  int bh = (padH - 3 * gap) / 4; // 4 rows

  // If Sci, Standard keys are shifted by 2 cols
  int colOffset = (currentMode == MODE_SCIENTIFIC || currentMode == MODE_UNIT ||
                   currentMode == MODE_RPN)
                      ? 2
                      : 0;
  int startX = 20;

  // Helper to place button
  // Row 1 - 7 8 9 *
  for (int i = 0; i < numButtons; i++) {
    if (strcmp(buttons[i].label, "7") == 0)
      buttons[i].rect =
          (SDL_Rect){startX + (0 + colOffset) * (bw + gap), startY, bw, bh};
  }
  for (int i = 0; i < numButtons; i++) {
    if (strcmp(buttons[i].label, "8") == 0)
      buttons[i].rect =
          (SDL_Rect){startX + (1 + colOffset) * (bw + gap), startY, bw, bh};
  }
  for (int i = 0; i < numButtons; i++) {
    if (strcmp(buttons[i].label, "9") == 0)
      buttons[i].rect =
          (SDL_Rect){startX + (2 + colOffset) * (bw + gap), startY, bw, bh};
  }
  for (int i = 0; i < numButtons; i++) {
    if (strcmp(buttons[i].label, "*") == 0)
      buttons[i].rect =
          (SDL_Rect){startX + (3 + colOffset) * (bw + gap), startY, bw, bh};
  }

  // Row 2 - 4 5 6 -
  for (int i = 0; i < numButtons; i++) {
    if (strcmp(buttons[i].label, "4") == 0)
      buttons[i].rect = (SDL_Rect){startX + (0 + colOffset) * (bw + gap),
                                   startY + (bh + gap), bw, bh};
  }
  for (int i = 0; i < numButtons; i++) {
    if (strcmp(buttons[i].label, "5") == 0)
      buttons[i].rect = (SDL_Rect){startX + (1 + colOffset) * (bw + gap),
                                   startY + (bh + gap), bw, bh};
  }
  for (int i = 0; i < numButtons; i++) {
    if (strcmp(buttons[i].label, "6") == 0)
      buttons[i].rect = (SDL_Rect){startX + (2 + colOffset) * (bw + gap),
                                   startY + (bh + gap), bw, bh};
  }
  for (int i = 0; i < numButtons; i++) {
    if (strcmp(buttons[i].label, "-") == 0)
      buttons[i].rect = (SDL_Rect){startX + (3 + colOffset) * (bw + gap),
                                   startY + (bh + gap), bw, bh};
  }

  // Row 3 - 1 2 3 +
  for (int i = 0; i < numButtons; i++) {
    if (strcmp(buttons[i].label, "1") == 0)
      buttons[i].rect = (SDL_Rect){startX + (0 + colOffset) * (bw + gap),
                                   startY + 2 * (bh + gap), bw, bh};
  }
  for (int i = 0; i < numButtons; i++) {
    if (strcmp(buttons[i].label, "2") == 0)
      buttons[i].rect = (SDL_Rect){startX + (1 + colOffset) * (bw + gap),
                                   startY + 2 * (bh + gap), bw, bh};
  }
  for (int i = 0; i < numButtons; i++) {
    if (strcmp(buttons[i].label, "3") == 0)
      buttons[i].rect = (SDL_Rect){startX + (2 + colOffset) * (bw + gap),
                                   startY + 2 * (bh + gap), bw, bh};
  }
  for (int i = 0; i < numButtons; i++) {
    if (strcmp(buttons[i].label, "+") == 0)
      buttons[i].rect = (SDL_Rect){startX + (3 + colOffset) * (bw + gap),
                                   startY + 2 * (bh + gap), bw, bh};
  }

  // Row 4 - 0 . =
  for (int i = 0; i < numButtons; i++) {
    if (strcmp(buttons[i].label, "0") == 0)
      buttons[i].rect = (SDL_Rect){startX + (0 + colOffset) * (bw + gap),
                                   startY + 3 * (bh + gap), 2 * bw + gap, bh};
  }
  for (int i = 0; i < numButtons; i++) {
    if (strcmp(buttons[i].label, ".") == 0)
      buttons[i].rect = (SDL_Rect){startX + (2 + colOffset) * (bw + gap),
                                   startY + 3 * (bh + gap), bw, bh};
  }
  for (int i = 0; i < numButtons; i++) {
    if (strcmp(buttons[i].label, "=") == 0 ||
        strcmp(buttons[i].label, "ENT") == 0)
      buttons[i].rect = (SDL_Rect){startX + (3 + colOffset) * (bw + gap),
                                   startY + 3 * (bh + gap), bw, bh};
  }

  // Scientific Keys
  if (currentMode == MODE_SCIENTIFIC) {
    // Col 0: sin, cos, tan, log
    // Col 1: sqrt, x^2, x^y, ln

    SDL_Rect r00 = {startX, startY, bw, bh};
    SDL_Rect r01 = {startX + bw + gap, startY, bw, bh};

    for (int i = 0; i < numButtons; i++) {
      if (strcmp(buttons[i].label, "sin") == 0)
        buttons[i].rect = r00;
    }
    for (int i = 0; i < numButtons; i++) {
      if (strcmp(buttons[i].label, "sqrt") == 0)
        buttons[i].rect = r01;
    }

    r00.y += bh + gap;
    r01.y += bh + gap;
    for (int i = 0; i < numButtons; i++) {
      if (strcmp(buttons[i].label, "cos") == 0)
        buttons[i].rect = r00;
    }
    for (int i = 0; i < numButtons; i++) {
      if (strcmp(buttons[i].label, "sqr") == 0)
        buttons[i].rect = r01;
    } // label "sqr" for x^2

    r00.y += bh + gap;
    r01.y += bh + gap;
    for (int i = 0; i < numButtons; i++) {
      if (strcmp(buttons[i].label, "tan") == 0)
        buttons[i].rect = r00;
    }
    for (int i = 0; i < numButtons; i++) {
      if (strcmp(buttons[i].label, "x^y") == 0)
        buttons[i].rect = r01;
    }

    r00.y += bh + gap;
    r01.y += bh + gap;
    for (int i = 0; i < numButtons; i++) {
      if (strcmp(buttons[i].label, "log") == 0)
        buttons[i].rect = r00;
    }
    for (int i = 0; i < numButtons; i++) {
      if (strcmp(buttons[i].label, "ln") == 0)
        buttons[i].rect = r01;
    }
  }

  // Unit Converter Keys (Replace Scientific)
  if (currentMode == MODE_UNIT) {
    SDL_Rect r00 = {startX, startY, bw, bh};
    SDL_Rect r01 = {startX + bw + gap, startY, bw, bh};

    for (int i = 0; i < numButtons; i++) {
      if (strcmp(buttons[i].label, "cm2in") == 0)
        buttons[i].rect = r00;
    }
    for (int i = 0; i < numButtons; i++) {
      if (strcmp(buttons[i].label, "in2cm") == 0)
        buttons[i].rect = r01;
    }

    r00.y += bh + gap;
    r01.y += bh + gap;
    for (int i = 0; i < numButtons; i++) {
      if (strcmp(buttons[i].label, "kg2lb") == 0)
        buttons[i].rect = r00;
    }
    for (int i = 0; i < numButtons; i++) {
      if (strcmp(buttons[i].label, "lb2kg") == 0)
        buttons[i].rect = r01;
    }

    r00.y += bh + gap;
    r01.y += bh + gap;
    for (int i = 0; i < numButtons; i++) {
      if (strcmp(buttons[i].label, "km2mi") == 0)
        buttons[i].rect = r00;
    }
    for (int i = 0; i < numButtons; i++) {
      if (strcmp(buttons[i].label, "mi2km") == 0)
        buttons[i].rect = r01;
    }

    r00.y += bh + gap;
    r01.y += bh + gap;
    for (int i = 0; i < numButtons; i++) {
      if (strcmp(buttons[i].label, "C2F") == 0)
        buttons[i].rect = r00;
    }
    for (int i = 0; i < numButtons; i++) {
      if (strcmp(buttons[i].label, "F2C") == 0)
        buttons[i].rect = r01;
    }
  }

  // RPN Keys
  if (currentMode == MODE_RPN) {
    // Logic for = becomes ENT
    // RPN buttons: SWP, DRP.
    // Place SWP, DRP in Col 0, 1 of Row 1?
    SDL_Rect r00 = {startX, startY, bw, bh};
    SDL_Rect r01 = {startX + bw + gap, startY, bw, bh};

    for (int i = 0; i < numButtons; i++) {
      if (strcmp(buttons[i].label, "SWP") == 0)
        buttons[i].rect = r00;
    }
    for (int i = 0; i < numButtons; i++) {
      if (strcmp(buttons[i].label, "DRP") == 0)
        buttons[i].rect = r01;
    }

    // Update label of '=' button to 'ENT' or vice versa.
    // We can't change label string easily if it's static literal?
    // buttons[i].label is char[8], so we can strcpy.
    for (int i = 0; i < numButtons; i++) {
      if (strcmp(buttons[i].label, "=") == 0 ||
          strcmp(buttons[i].label, "ENT") == 0) {
        strcpy(buttons[i].label, "ENT");
        // Color? ENT typical is large vertical, here just 1x1. keep Orange.
      }
    }
  } else {
    // Restore '=' if not RPN
    for (int i = 0; i < numButtons; i++) {
      if (strcmp(buttons[i].label, "=") == 0 ||
          strcmp(buttons[i].label, "ENT") == 0) {
        strcpy(buttons[i].label, "=");
      }
    }
  }
}

void initButtons(void) {
  // Create buttons with dummy rects, updateLayout will fix them
  // We just need to set labels and colors
  numButtons = 0;

  // Digit buttons 1-9
  int nums[3][3] = {{7, 8, 9}, {4, 5, 6}, {1, 2, 3}};
  for (int row = 0; row < 3; row++) {
    for (int col = 0; col < 3; col++) {
      Button *b = &buttons[numButtons++];
      snprintf(b->label, sizeof(b->label), "%d", nums[row][col]);
      b->color = COLOR_DIGIT;
    }
  }

  // 0 button
  Button *b0 = &buttons[numButtons++];
  strcpy(b0->label, "0");
  b0->color = COLOR_DIGIT;

  // . button
  Button *bdot = &buttons[numButtons++];
  strcpy(bdot->label, ".");
  bdot->color = COLOR_DIGIT;

  // = button
  Button *beq = &buttons[numButtons++];
  strcpy(beq->label, "=");
  beq->color = COLOR_OP;

  // Operators
  char ops[4] = {'+', '-', '*', '/'};
  for (int i = 0; i < 4; i++) {
    Button *bop = &buttons[numButtons++];
    snprintf(bop->label, sizeof(bop->label), "%c", ops[i]);
    bop->color = COLOR_OP;
  }

  // Global C Button
  strcpy(cBtn.label, "C");
  cBtn.color = COLOR_CLEAR;

  // History Toggle Button
  strcpy(histBtn.label, "HIST");
  histBtn.color = COLOR_OP;

  // Draw Toggle Button - No longer used as button, now in mode.
  // We can keep it or clear it. Let's keep data valid.
  strcpy(drawBtn.label, "DRAW");
  drawBtn.color = (SDL_Color){0, 150, 200, 255};

  // Mode Button
  strcpy(modeBtn.label, "MODE");
  modeBtn.color = (SDL_Color){100, 100, 255, 255};

  // Create Scientific Buttons if not already
  // Add: sin cos tan log ln sqrt sqr x^y PI e (10 buttons)
  // Check if numButtons < 20 to avoid re-adding (standard 16 + 10 sci + 8 unit
  // + 2 rpn)
  if (numButtons < 20) {
    char *sciLabels[] = {"sin",  "cos", "tan", "log", "ln",
                         "sqrt", "sqr", "x^y", "PI",  "e"};
    for (int i = 0; i < 10; i++) {
      Button *b = &buttons[numButtons++];
      strcpy(b->label, sciLabels[i]);
      b->color = COLOR_OP; // CHANGED to same as operators
    }

    // Unit buttons
    // Add new ones: km2mi, mi2km, C2F, F2C
    char *unitLabels[] = {"cm2in", "in2cm", "kg2lb", "lb2kg",
                          "km2mi", "mi2km", "C2F",   "F2C"};
    for (int i = 0; i < 8; i++) {
      Button *b = &buttons[numButtons++];
      strcpy(b->label, unitLabels[i]);
      b->color = COLOR_OP;
    }

    // RPN buttons: SWP, DRP
    char *rpnLabels[] = {"SWP", "DRP"};
    for (int i = 0; i < 2; i++) {
      Button *b = &buttons[numButtons++];
      strcpy(b->label, rpnLabels[i]);
      b->color = COLOR_OP;
    }
  }

  // Initial layout
  updateLayout(300, 390);
}

// Global window pointer moved to top

void handleButtonClick(int x, int y) {
  SDL_Window *win = gWindow;
  int w, h;
  SDL_GetWindowSize(win, &w, &h);

  // History Button
  if (x >= histBtn.rect.x && x < histBtn.rect.x + histBtn.rect.w &&
      y >= histBtn.rect.y && y < histBtn.rect.y + histBtn.rect.h) {
    showHistory = !showHistory;
    if (showHistory)
      showDraw = 0; // Close draw panel if opening history
    SDL_SetWindowSize(win, showHistory ? w + 200 : w - 200,
                      h); // Adjust width based on current width
    updateLayout(w, h);   // Recalculate layout for new window size
    triggerClickAnim(2, 0);
    return;
  }

  // Mode Button
  if (x >= modeBtn.rect.x && x < modeBtn.rect.x + modeBtn.rect.w &&
      y >= modeBtn.rect.y && y < modeBtn.rect.y + modeBtn.rect.h) {
    isDropdownOpen = !isDropdownOpen;
    return;
  }

  // Dropdown options
  if (isDropdownOpen) {
    // 4 options: Basic, Scientific, Unit, Draw
    // Drawn from modeBtn.y + h downwards
    SDL_Rect r = modeBtn.rect;
    int itemH = 30;

    // Basic
    if (x >= r.x && x < r.x + 100 && y >= r.y + r.h && y < r.y + r.h + itemH) {
      currentMode = MODE_BASIC;
      isDropdownOpen = 0;
      showDraw = 0;                   // Disable draw
      SDL_SetWindowSize(win, 300, h); // Set to basic width
      updateLayout(300, h);
      return;
    }
    // Sci
    if (x >= r.x && x < r.x + 100 && y >= r.y + r.h + itemH &&
        y < r.y + r.h + 2 * itemH) {
      currentMode = MODE_SCIENTIFIC;
      isDropdownOpen = 0;
      showDraw = 0;
      SDL_SetWindowSize(win, 450, h); // Set to scientific width
      updateLayout(450, h);
      return;
    }
    // Unit
    if (x >= r.x && x < r.x + 100 && y >= r.y + r.h + 2 * itemH &&
        y < r.y + r.h + 3 * itemH) {
      currentMode = MODE_UNIT;
      isDropdownOpen = 0;
      showDraw = 0;
      SDL_SetWindowSize(win, 450, h); // Unit mode is wide
      updateLayout(450, h);
      return;
    }
    // RPN
    if (x >= r.x && x < r.x + 100 && y >= r.y + r.h + 3 * itemH &&
        y < r.y + r.h + 4 * itemH) {
      currentMode = MODE_RPN;
      isDropdownOpen = 0;
      showDraw = 0;
      SDL_SetWindowSize(win, 650,
                        h); // RPN mode is wide, sidebar handled by updateLayout
      updateLayout(650, h);
      return;
    }
    // Draw
    if (x >= r.x && x < r.x + 100 && y >= r.y + r.h + 4 * itemH &&
        y < r.y + r.h + 5 * itemH) {
      currentMode = MODE_DRAW; // This handles layout logic
      // Trigger old showDraw flag logic
      showDraw = 1;
      showHistory = 0;
      isDropdownOpen = 0;
      SDL_SetWindowSize(win, 300, h); // Draw mode is basic width
      updateLayout(300, h);           // will update grid
      return;
    }

    // Click outside -> close
    isDropdownOpen = 0;
    return;
  }

  // Don't process other buttons if dropdown was open?
  // Should have returned above.

  // C Button
  if (x >= cBtn.rect.x && x < cBtn.rect.x + cBtn.rect.w && y >= cBtn.rect.y &&
      y < cBtn.rect.y + cBtn.rect.h) {
    calc_inputClear();
    triggerClickAnim(1, 0);
    return;
  }

  // Draw Button - REMOVE CLICK logic as it is replaced by mode
  // But we kept it in render for Basic/Draw mode?
  // Actually plan said we replace DRAW with MODE button.
  // So DRAW button logic is effectively dead or hidden.
  // We assume MODE button replaces it in the UI.

  // Draw grid click (grid replaces keypad area)
  // ONLY IF IN DRAW MODE
  if (showDraw || currentMode == MODE_DRAW) {
    int calcWidth = showHistory ? w - 200 : w;
    int gridStartY = 120; // Correct startY for grid in click handler too

    int reservedBottom = 50; // Reserve space for buttons
    int padH = h - gridStartY - 20 - reservedBottom;
    int padW = calcWidth - 40;
    if (padH < 100)
      padH = 100; // Minimum safety

    int cellSize = (padW < padH ? padW : padH) / 28;
    int gridSize = 28 * cellSize;
    int gridX = 20 + (padW - gridSize) / 2;
    int gridY = gridStartY + (padH - gridSize) / 2;

    // Button Row Handling
    // Buttons: + - * / CLR (5 buttons)
    // Maybe = too? Let's do + - * / = CLR (6 buttons) to be complete
    // Actually simplicity: + - * / CLR
    int numDrBtns = 5;
    char *drLabels[] = {"+", "-", "*", "/", "CLR"};
    int gap = 10;
    int totalGap = (numDrBtns - 1) * gap;
    int availW = padW; // Use full width
    int btnW = (availW - totalGap) / numDrBtns;
    int btnH = 30;
    int btnY = gridStartY + padH + 10; // Below allocated grid area
    int startBtnX = 20;

    for (int i = 0; i < numDrBtns; i++) {
      SDL_Rect r = {startBtnX + i * (btnW + gap), btnY, btnW, btnH};
      if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) {
        triggerClickAnim(4, i);
        if (strcmp(drLabels[i], "CLR") == 0) {
          for (int r = 0; r < 28; r++) {
            for (int c = 0; c < 28; c++) {
              drawGrid[r][c] = 0;
            }
          }
          hasDrawnSomething = 0;
          return;
        } else {
          // Operator
          calc_inputOperator(drLabels[i][0]);
          // Visual feedback or simple return?
          return;
        }
      }
    }

    // Grid Click
    if (x >= gridX && x < gridX + gridSize && y >= gridY &&
        y < gridY + gridSize) {
      int col = (x - gridX) / cellSize;
      int row = (y - gridY) / cellSize;
      if (col >= 0 && col < 28 && row >= 0 && row < 28) {
        drawGrid[row][col] = 1;        // Set pixel ON (for drag drawing)
        isDrawing = 1;                 // Start drawing
        lastDrawTime = SDL_GetTicks(); // Track last draw time
        hasDrawnSomething = 1;         // Mark that we have content
      }
      return;
    }
    return; // Don't process keypad clicks when in draw mode
  }

  // History sidebar click
  if (showHistory) {
    if (x > w - 200) {
      int startY = 20;
      int itemH = 40;
      int index = (y - startY) / itemH;
      if (index >= 0 && index < historyCount) {
        loadHistory(index);
      }
      return;
    }
  }

  // Calculator buttons
  for (int i = 0; i < numButtons; i++) {
    SDL_Rect *r = &buttons[i].rect;
    // Only process if button is visible (has a non-zero width/height)
    if (r->w > 0 && r->h > 0 && x >= r->x && x < r->x + r->w && y >= r->y &&
        y < r->y + r->h) {
      char *label = buttons[i].label;
      if ((label[0] >= '0' && label[0] <= '9') || label[0] == '.') {
        calc_inputDigit(label);
      } else if (label[0] == '+' || label[0] == '-' || label[0] == '*' ||
                 label[0] == '/' || label[0] == '^') {
        calc_inputOperator(label[0]);
      } else if (strcmp(label, "=") == 0) { // Basic mode equals
        calc_inputEquals();
      } else if (strcmp(label, "ENT") == 0) { // RPN Enter
        calc_inputRPN("ENT");
      } else if (strcmp(label, "sin") == 0 || strcmp(label, "cos") == 0 ||
                 strcmp(label, "tan") == 0 || strcmp(label, "log") == 0 ||
                 strcmp(label, "ln") == 0 || strcmp(label, "sqr") == 0 ||
                 strcmp(label, "sqrt") == 0) {
        calc_inputUnary(label);
      } else if (strcmp(label, "x^y") == 0) {
        calc_inputOperator('^');
      } else if (strcmp(label, "PI") == 0 || strcmp(label, "e") == 0) {
        calc_inputConstant(label);
      } else if (strcmp(label, "cm2in") == 0 || strcmp(label, "in2cm") == 0 ||
                 strcmp(label, "kg2lb") == 0 || strcmp(label, "lb2kg") == 0 ||
                 strcmp(label, "km2mi") == 0 || strcmp(label, "mi2km") == 0 ||
                 strcmp(label, "C2F") == 0 || strcmp(label, "F2C") == 0) {
        calc_inputUnit(label);
      } else if (strcmp(label, "SWP") == 0 || strcmp(label, "DRP") == 0) {
        calc_inputRPN(label);
      }
      triggerClickAnim(0, i);
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

  // Period
  if (key == SDLK_PERIOD || key == SDLK_KP_PERIOD) {
    calc_inputDigit(".");
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
  case SDLK_CARET: // For '^'
    calc_inputOperator('^');
    break;
  case SDLK_EQUALS:
  case SDLK_KP_EQUALS:
  case SDLK_RETURN:
  case SDLK_KP_ENTER:
    if (currentMode == MODE_RPN) {
      calc_inputRPN("ENT");
    } else {
      calc_inputEquals();
    }
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
  int w, h;
  SDL_GetWindowSize(gWindow, &w, &h); // Get current window size

  static int lastW = 0, lastH = 0;

  // Fake Crash Render
  if (isCrashMode) {
    // Recovery check
    if (SDL_GetTicks() - crashStartTime > 2000) {
      isCrashMode = 0;
      divZeroCount = 0;
      SDL_SetWindowFullscreen(gWindow, 0); // Restore windowed
      calc_inputClear();
      // Force layout update next frame
      lastW = 0;
    } else {
      // Render Glitch/Crash
      SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
      SDL_RenderClear(ren);

      // "segmentation fault" text
      // Random offset
      int offX = rand() % 5 - 2;
      int offY = rand() % 5 - 2;

      SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
      drawText(ren, "segmentation fault", 50 + offX, 50 + offY, 4);

      // Random glitch rectangles
      for (int i = 0; i < 10; i++) {
        if (rand() % 100 < 20) {
          SDL_Rect r = {rand() % w, rand() % h, rand() % 100, rand() % 5};
          SDL_SetRenderDrawColor(ren, rand() % 255, rand() % 255, rand() % 255,
                                 255);
          SDL_RenderFillRect(ren, &r);
        }
      }

      SDL_RenderPresent(ren);
      return; // Early return to block normal UI
    }
  }

  if (w != lastW || h != lastH) {
    updateLayout(w, h);
    lastW = w;
    lastH = h;
  }

  // Background
  SDL_SetRenderDrawColor(ren, COLOR_BG.r, COLOR_BG.g, COLOR_BG.b, 255);
  SDL_RenderClear(ren);

  // --- Sidebar (History OR RPN Stack) ---
  // The instruction about `calcWidth` in `updateLayout` is not directly
  // applicable here as `updateLayout` function definition is not in the
  // provided content. However, the `calcWidth` variable is used in `render` for
  // `showDraw` logic. The instruction implies that the sidebar should also
  // appear for RPN mode. The `if (showHistory || currentMode == MODE_RPN)`
  // condition already handles this.
  if (showHistory || currentMode == MODE_RPN) {

    int sidebarX = w - 200;
    SDL_Rect sidebar = {sidebarX, 0, 200, h};
    SDL_SetRenderDrawColor(ren, 40, 40, 40, 255);
    SDL_RenderFillRect(ren, &sidebar);

    // Separator line
    SDL_SetRenderDrawColor(ren, 60, 60, 60, 255);
    SDL_RenderDrawLine(ren, sidebarX, 0, sidebarX, h);

    if (currentMode == MODE_RPN) {
      // Draw Stack (T, Z, Y, X)
      // X is display logic usually, but let's show stack[0] as X.
      // Stack indices: 0=X, 1=Y, 2=Z, 3=T.
      char *labels[] = {"X:", "Y:", "Z:", "T:"};
      for (int i = 0; i < 4; i++) {
        int y = h - 100 - i * 50;
        char valStr[64];
        snprintf(valStr, sizeof(valStr), "%.10g", calc.stack[i]);

        SDL_SetRenderDrawColor(ren, 150, 150, 150, 255);
        drawText(ren, labels[i], sidebarX + 10, y, 3);
        SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
        drawText(ren, valStr, sidebarX + 40, y, 3);
      }
      SDL_SetRenderDrawColor(ren, 200, 200, 200, 255);
      drawText(ren, "STACK", sidebarX + 10, 20, 3);

    } else {
      // History Items
      for (int i = 0; i < historyCount; i++) {
        int y = 20 + i * 40;

        // Equation centered/leftish of sidebar
        SDL_SetRenderDrawColor(ren, 150, 150, 150, 255);
        drawText(ren, history[i].equation, sidebarX + 10, y, 2);

        // Result
        char resStr[32];
        snprintf(resStr, sizeof(resStr), "%g", history[i].result);
        // Format
        char fmtRes[64];
        formatNumber(resStr, fmtRes, sizeof(fmtRes));

        SDL_SetRenderDrawColor(ren, COLOR_TEXT.r, COLOR_TEXT.g, COLOR_TEXT.b,
                               255);
        drawText(ren, fmtRes, sidebarX + 10, y + 15, 3);
      }
    }
  }

  // --- Calculator ---

  // Display background (use global displayRect)
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

  // Draw Mode Button
  SDL_SetRenderDrawColor(ren, modeBtn.color.r, modeBtn.color.g, modeBtn.color.b,
                         255);
  SDL_RenderFillRect(ren, &modeBtn.rect);
  SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
  drawText(ren, "MODE", modeBtn.rect.x + 5, modeBtn.rect.y + 10, 2);

  // Draw HIST button
  SDL_Color hc = histBtn.color;
  if (clickAnimType == 2 && SDL_GetTicks() - clickAnimTime < 150)
    hc = (SDL_Color){255, 200, 100, 255};
  SDL_SetRenderDrawColor(ren, hc.r, hc.g, hc.b, 255);
  SDL_RenderFillRect(ren, &histBtn.rect);
  SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
  drawText(ren, "HIST", histBtn.rect.x + 14, histBtn.rect.y + 10, 2);

  // Draw C Button (Always visible now)
  SDL_Color cc = cBtn.color;
  if (clickAnimType == 1 && SDL_GetTicks() - clickAnimTime < 150)
    cc = (SDL_Color){200, 200, 200, 255};
  SDL_SetRenderDrawColor(ren, cc.r, cc.g, cc.b, 255);
  SDL_RenderFillRect(ren, &cBtn.rect);
  SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
  drawText(ren, "C", cBtn.rect.x + 20, cBtn.rect.y + 10, 2);

  // --- Draw Panel (16x16 Grid) replacing keypad ---
  if (showDraw || currentMode == MODE_DRAW) {
    int calcWidth = showHistory ? w - 200 : w;
    // START GRID AT 120 (Below control row)
    int gridStartY = 120; // Correct startY for grid to maximize space

    int reservedBottom = 50;
    int padH = h - gridStartY - 20 - reservedBottom;
    int padW = calcWidth - 40;
    if (padH < 100)
      padH = 100;

    int cellSize = (padW < padH ? padW : padH) / 28;
    int gridSize = 28 * cellSize;
    int gridX = 20 + (padW - gridSize) / 2;
    int gridY = gridStartY + (padH - gridSize) / 2;

    for (int row = 0; row < 28; row++) {
      for (int col = 0; col < 28; col++) {
        SDL_Rect cell = {gridX + col * cellSize, gridY + row * cellSize,
                         cellSize - 1, cellSize - 1};
        if (drawGrid[row][col]) {
          SDL_SetRenderDrawColor(ren, 50, 50, 50, 255);
        } else {
          SDL_SetRenderDrawColor(ren, 200, 200, 200, 255);
        }
        SDL_RenderFillRect(ren, &cell);
      }
    }

    // Grid border
    SDL_SetRenderDrawColor(ren, 100, 100, 100, 255);
    SDL_Rect gridBorder = {gridX - 1, gridY - 1, gridSize + 1, gridSize + 1};
    SDL_RenderDrawRect(ren, &gridBorder);

    // Draw buttons: + - * / CLR
    int numDrBtns = 5;
    char *drLabels[] = {"+", "-", "*", "/", "CLR"};
    SDL_Color drColors[] = {COLOR_OP, COLOR_OP, COLOR_OP, COLOR_OP,
                            COLOR_CLEAR};
    int gap = 10;
    int totalGap = (numDrBtns - 1) * gap;
    int availW = padW;
    int btnW = (availW - totalGap) / numDrBtns;
    int btnH = 30;
    int btnY = gridStartY + padH + 10;
    int startBtnX = 20;

    for (int i = 0; i < numDrBtns; i++) {
      SDL_Rect r = {startBtnX + i * (btnW + gap), btnY, btnW, btnH};

      SDL_Color bg = drColors[i];
      if (clickAnimType == 4 && clickAnimIdx == i &&
          SDL_GetTicks() - clickAnimTime < 150) {
        bg.r = (bg.r + 100) > 255 ? 255 : (bg.r + 100);
        bg.g = (bg.g + 100) > 255 ? 255 : (bg.g + 100);
        bg.b = (bg.b + 100) > 255 ? 255 : (bg.b + 100);
      }

      SDL_SetRenderDrawColor(ren, bg.r, bg.g, bg.b, 255);
      SDL_RenderFillRect(ren, &r);

      SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
      // Center text
      int labelLen = strlen(drLabels[i]);
      int lx = r.x +
               (r.w - labelLen * 12) / 2; // using ~12px per char width estimate

      int ly = r.y + (r.h - 15) / 2;
      drawText(ren, drLabels[i], lx, ly, 3);
    }

  } else {
    // Buttons (only show when not in draw mode)
    for (int i = 0; i < numButtons; i++) {
      Button *b = &buttons[i];

      // Only draw if the button has a valid size (set by updateLayout)
      if (b->rect.w <= 0 || b->rect.h <= 0)
        continue;

      SDL_Color bg = b->color;
      if (clickAnimType == 0 && clickAnimIdx == i &&
          SDL_GetTicks() - clickAnimTime < 150) {
        bg.r = (bg.r + 60) > 255 ? 255 : (bg.r + 60);
        bg.g = (bg.g + 60) > 255 ? 255 : (bg.g + 60);
        bg.b = (bg.b + 60) > 255 ? 255 : (bg.b + 60);
      }
      SDL_SetRenderDrawColor(ren, bg.r, bg.g, bg.b, 255);
      SDL_RenderFillRect(ren, &b->rect);

      // Button label (centered)
      SDL_SetRenderDrawColor(ren, COLOR_TEXT.r, COLOR_TEXT.g, COLOR_TEXT.b,
                             255);
      int labelLen = strlen(b->label);
      int lx = b->rect.x + (b->rect.w - labelLen * 12) / 2;
      int ly = b->rect.y + (b->rect.h - 15) / 2;
      drawText(ren, b->label, lx, ly, 3);
    }
  }

  // Draw Dropdown Menu if open (on top)
  if (isDropdownOpen) {
    SDL_Rect r = modeBtn.rect;
    r.y += r.h;
    r.w = 100; // Fixed width for menu
    r.h = 150; // 5 items * 30

    // Background
    SDL_SetRenderDrawColor(ren, 40, 40, 40, 255);
    SDL_RenderFillRect(ren, &r);

    // Items
    SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
    drawText(ren, "Basic", r.x + 10, r.y + 5, 2);
    drawText(ren, "Scientific", r.x + 10, r.y + 35, 2);
    drawText(ren, "Unit", r.x + 10, r.y + 65, 2);
    drawText(ren, "RPN", r.x + 10, r.y + 95, 2);
    drawText(ren, "Draw", r.x + 10, r.y + 125, 2);

    // Borders
    SDL_SetRenderDrawColor(ren, 100, 100, 100, 255);
    SDL_RenderDrawRect(ren, &r);
  }

  // Heart Animation Overlay
  if (isHeartAnimActive) {
    Uint32 now = SDL_GetTicks();
    Uint32 elapsed = now - easterEggStart;
    Uint32 duration = 2000; // 2 seconds

    if (elapsed < duration) {
      float progress = (float)elapsed / duration;
      int alpha = (int)(255 * (1.0f - progress));
      if (alpha < 0)
        alpha = 0;

      // Draw a big heart in the center
      // We can just iterate over the template we made earlier, but we can't
      // access it easily if it's static inside predictDigit. Oops. Let's just
      // draw a big red rectangle or approximate heart for now, or move the
      // template global. Moving template global is better. But I can't edit
      // that line again easily in this chunk. I'll just draw a procedural heart
      // shape or use SDL_RenderDrawLines. Or just a filled red circle/rect is
      // lame. Let's make a local pattern for the big heart. Actually, let's
      // just draw 2 overlapping circles and a triangle.

      SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(ren, 255, 0, 0, alpha);

      // Center of window
      int cx = w / 2;
      int cy = h / 2;
      int size = 100;

      // Triangle bottom
      // SDL doesn't have fillTriangle.
      // Let's just draw a big red square for simplicity? No request said
      // "Heart". "draw heart on the screen a big translucent heart appears"
      // Okay I should try to make it look like a heart.
      // I will draw a collection of rects based on a coarse bitmap.
      int heartMap[10][11] = {
          {0, 1, 1, 0, 0, 0, 1, 1, 0, 0, 0}, {1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 0},
          {1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0}, {1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0},
          {0, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0}, {0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 0},
          {0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0},
      };

      // Draw this scaled up
      int sqSize = 15;
      int startX = cx - (11 * sqSize) / 2;
      int startY = cy - (8 * sqSize) / 2;

      for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 11; c++) {
          if (heartMap[r][c]) {
            SDL_Rect rect = {startX + c * sqSize, startY + r * sqSize, sqSize,
                             sqSize};
            SDL_RenderFillRect(ren, &rect);
          }
        }
      }
      SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);

    } else {
      isHeartAnimActive = 0;
    }
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

  // Load ML Model
  if (model_load("model.bin", &nn)) {
    printf("Successfully loaded model.bin\n");
    modelLoaded = 1;
  } else {
    printf("Failed to load model.bin! Make sure to run ./train first.\n");
  }

  initButtons();

  int quit = 0;
  SDL_Event e;

  while (!quit) {
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) {
        quit = 1;
      } else if (e.type == SDL_MOUSEBUTTONDOWN) {
        handleButtonClick(e.button.x, e.button.y);
      } else if (e.type == SDL_MOUSEBUTTONUP) {
        isDrawing = 0; // Stop drawing when mouse released
      } else if (e.type == SDL_MOUSEMOTION && isDrawing && showDraw) {
        // Handle drag drawing on the grid
        int x = e.motion.x;
        int y = e.motion.y;
        int w, h;
        SDL_GetWindowSize(gWindow, &w, &h);
        int calcWidth = showHistory ? w - 200 : w;
        int startY = 120;
        int reservedBottom = 50;
        int padH = h - startY - 20 - reservedBottom;
        int padW = calcWidth - 40;
        if (padH < 100)
          padH = 100;

        int cellSize = (padW < padH ? padW : padH) / 28;
        int gridSize = 28 * cellSize;
        int gridX = 20 + (padW - gridSize) / 2;
        int gridY = startY + (padH - gridSize) / 2;

        if (x >= gridX && x < gridX + gridSize && y >= gridY &&
            y < gridY + gridSize) {
          int col = (x - gridX) / cellSize;
          int row = (y - gridY) / cellSize;
          if (col >= 0 && col < 28 && row >= 0 && row < 28) {
            drawGrid[row][col] = 1;        // Set pixel ON while dragging
            lastDrawTime = SDL_GetTicks(); // Track last draw time
            hasDrawnSomething = 1;
          }
        }
      } else if (e.type == SDL_KEYDOWN) {
        handleKeyboard(e.key.keysym.sym);
      }
    }

    // Auto-recognition: predict after user stops drawing
    if (showDraw && hasDrawnSomething && !isDrawing) {
      Uint32 now = SDL_GetTicks();
      if (now - lastDrawTime > AUTO_PREDICT_DELAY) {
        predictedDigit = predictDigit();

        if (predictedDigit == -2) {
          // Heart detected!
          isHeartAnimActive = 1;
          easterEggStart = SDL_GetTicks();
        } else {
          // Insert predicted digit into calculator display
          char digit[2] = {'0' + predictedDigit, '\0'};
          calc_inputDigit(digit);
        }

        // Clear grid for next drawing (keep draw mode open)
        for (int r = 0; r < 28; r++) {
          for (int c = 0; c < 28; c++) {
            drawGrid[r][c] = 0;
          }
        }
        hasDrawnSomething = 0;
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
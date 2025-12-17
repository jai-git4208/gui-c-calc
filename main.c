#include "model.h"
#include "nanovg.h"
#include <OpenGL/gl3.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define NANOVG_GL3_IMPLEMENTATION
#include "nanovg_gl.h"

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
  float x, y, w, h; // Float for NanoVG
  char label[16];
  int role; // 0=Digit, 1=Op, 2=Action/Control
  int is_hovered;
  int is_pressed;
  float anim_t;   // 0.0 -> 1.0 hover animation
  NVGcolor color; // Base color
} Button;

// Theme
typedef struct {
  NVGcolor bg;
  NVGcolor display_bg;
  NVGcolor btn_bg_digit;
  NVGcolor btn_bg_op;
  NVGcolor btn_bg_action;
  NVGcolor text_primary;
  NVGcolor text_secondary;
  NVGcolor shadow;
} UITheme;

UITheme theme_light;
UITheme theme_dark;
UITheme *current_theme;
NVGcontext *vg = NULL;
int fontNormal, fontBold;

// Colors
// Old Colors (Migration: Use theme instead)
// Keeping these for legacy code compilation until fully migrated, but they are
// unused in new UI.
SDL_Color COLOR_BG = {30, 30, 30, 255};
SDL_Color _COLOR_DIGIT = {70, 70, 70, 255}; // Renamed to avoid usage
SDL_Color _COLOR_OP = {255, 149, 0, 255};
SDL_Color _COLOR_CLEAR = {165, 165, 165, 255};
SDL_Color COLOR_TEXT = {255, 255, 255, 255};
SDL_Color COLOR_DISPLAY = {45, 45, 45, 255};

// Global calculator
Calculator calc = {"0", 0, 0, 0, 0, {0, 0, 0, 0}, 0};
Button buttons[40];
int numButtons = 0;

// Fake Crash State
int divZeroCount = 0;
int isCrashMode = 0;
Uint32 crashStartTime = 0;

// --- Globals ---
SDL_Window *gWindow = NULL;
TTF_Font *gFont = NULL;
TTF_Font *gFontLarge = NULL;
int winWidth = 232;
int winHeight = 306;

// Window Dragging
int isWindowDragging = 0;
int dragOffsetX = 0;
int dragOffsetY = 0;

// Modes
typedef enum {
  MODE_BASIC,
  MODE_DRAW,
  MODE_SCIENTIFIC,
  MODE_UNIT,
  MODE_RPN
} CalculatorMode;
CalculatorMode currentMode = MODE_BASIC;
int showHistory = 0;
int isDropdownOpen = 0;

// Global C Button
Button cBtn;

// Scientific Mode State
// This section was replaced by the new globals above.
// CalculatorMode currentMode = MODE_BASIC; // This is now part of the new
// globals int isDropdownOpen = 0; // This is now part of the new globals
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

// Persistence forward decl
void save_state(void);
void load_state(void);

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
  save_state(); // Save on every operation

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

  // ALWAYS null terminate
  dest[destIdx] = '\0';
}
// Old rendering helpers removed

// Global variables for history toggle
// showHistory is defined above
Button histBtn;

// Global variables for draw mode
int showDraw = 0;
Button drawBtn;
unsigned char drawGrid[28][28] = {0};
int isDrawing = 0;

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
    // User drawings are messy, so 0.45 might be good enough if they try to
    // fill it? Or if it's just outline? The template is filled. If user draws
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

// Display dimensions
float displayX, displayY, displayW, displayH;

void updateLayout(int width, int height) {
  int sidebarWidth = (showHistory || currentMode == MODE_RPN) ? 200 : 0;

  if (currentMode == MODE_SCIENTIFIC || currentMode == MODE_UNIT ||
      currentMode == MODE_RPN) {
    // Width override removed for responsiveness
  } else {
    // Basic / Draw Mode - allow smaller width
  }

  int controlY = 80;
  int controlH = 30;
  int startY = 120;

  int calcWidth = width - ((showHistory || currentMode == MODE_RPN) ? 200 : 0);
  int padW = calcWidth - 40;

  // Re-calc display
  displayX = 20;
  displayY = 20;
  displayW = calcWidth - 40;
  displayH = 50;

  // Reset keys
  for (int i = 0; i < numButtons; i++) {
    buttons[i].x = 0;
    buttons[i].y = 0;
    buttons[i].w = 0;
    buttons[i].h = 0;
  }
  cBtn.x = 0;
  cBtn.y = 0;
  cBtn.w = 0;
  cBtn.h = 0;
  histBtn.x = 0;
  histBtn.y = 0;
  histBtn.w = 0;
  histBtn.h = 0;
  drawBtn.x = 0;
  drawBtn.y = 0;
  drawBtn.w = 0;
  drawBtn.h = 0;
  modeBtn.x = 0;
  modeBtn.y = 0;
  modeBtn.w = 0;
  modeBtn.h = 0;

  int numControl = (currentMode == MODE_SCIENTIFIC ||
                    currentMode == MODE_UNIT || currentMode == MODE_RPN)
                       ? 6
                       : 4;
  int gap = 10;
  float ctrlBtnW = (float)(padW - gap * (numControl - 1)) / numControl;

  // Mode & Hist
  modeBtn.x = 20;
  modeBtn.y = controlY;
  modeBtn.w = ctrlBtnW;
  modeBtn.h = controlH;
  histBtn.x = 20 + ctrlBtnW + gap;
  histBtn.y = controlY;
  histBtn.w = ctrlBtnW;
  histBtn.h = controlH;
  cBtn.x = 20 + 2 * (ctrlBtnW + gap);
  cBtn.y = controlY;
  cBtn.w = ctrlBtnW;
  cBtn.h = controlH;

  // Update '/'
  if (currentMode != MODE_DRAW && !showDraw) {
    for (int i = 0; i < numButtons; i++) {
      if (strcmp(buttons[i].label, "/") == 0) {
        buttons[i].x = 20 + 3 * (ctrlBtnW + gap);
        buttons[i].y = controlY;
        buttons[i].w = ctrlBtnW;
        buttons[i].h = controlH;
      }
    }
  }

  if (currentMode == MODE_SCIENTIFIC || currentMode == MODE_UNIT ||
      currentMode == MODE_RPN) {
    for (int i = 0; i < numButtons; i++) {
      if (strcmp(buttons[i].label, "PI") == 0) {
        buttons[i].x = 20 + 4 * (ctrlBtnW + gap);
        buttons[i].y = controlY;
        buttons[i].w = ctrlBtnW;
        buttons[i].h = controlH;
      }
      if (strcmp(buttons[i].label, "e") == 0) {
        buttons[i].x = 20 + 5 * (ctrlBtnW + gap);
        buttons[i].y = controlY;
        buttons[i].w = ctrlBtnW;
        buttons[i].h = controlH;
      }
    }
  }

  // Keypad
  int cols = (currentMode == MODE_SCIENTIFIC || currentMode == MODE_UNIT ||
              currentMode == MODE_RPN)
                 ? 6
                 : 4;
  float bw = (float)(padW - gap * (cols - 1)) / cols;

  int padH = height - startY - 20;
  // Removed hard clamp so buttons shrink to fit
  if (padH < 50)
    padH = 50; // Minimal safety only
  float bh = (float)(padH - 3 * gap) / 4;

  int colOffset = (currentMode == MODE_SCIENTIFIC || currentMode == MODE_UNIT ||
                   currentMode == MODE_RPN)
                      ? 2
                      : 0;
  int startX = 20;

  // Set positions
  if (currentMode != MODE_DRAW && !showDraw) {
    for (int i = 0; i < numButtons; i++) {
      int r = -1, c = -1;
      char *l = buttons[i].label;

      // Manual mapping for standard keys
      if (strcmp(l, "7") == 0) {
        r = 0;
        c = 0;
      } else if (strcmp(l, "8") == 0) {
        r = 0;
        c = 1;
      } else if (strcmp(l, "9") == 0) {
        r = 0;
        c = 2;
      } else if (strcmp(l, "*") == 0) {
        r = 0;
        c = 3;
      } else if (strcmp(l, "4") == 0) {
        r = 1;
        c = 0;
      } else if (strcmp(l, "5") == 0) {
        r = 1;
        c = 1;
      } else if (strcmp(l, "6") == 0) {
        r = 1;
        c = 2;
      } else if (strcmp(l, "-") == 0) {
        r = 1;
        c = 3;
      } else if (strcmp(l, "1") == 0) {
        r = 2;
        c = 0;
      } else if (strcmp(l, "2") == 0) {
        r = 2;
        c = 1;
      } else if (strcmp(l, "3") == 0) {
        r = 2;
        c = 2;
      } else if (strcmp(l, "+") == 0) {
        r = 2;
        c = 3;
      } else if (strcmp(l, ".") == 0) {
        r = 3;
        c = 2;
      } else if (strcmp(l, "=") == 0 || strcmp(l, "ENT") == 0) {
        r = 3;
        c = 3;
      }

      if (r != -1) {
        buttons[i].x = startX + (c + colOffset) * (bw + gap);
        buttons[i].y = startY + r * (bh + gap);
        buttons[i].w = bw;
        buttons[i].h = bh;
      }

      // 0 Button (Wide)
      if (strcmp(l, "0") == 0) {
        buttons[i].x = startX + (0 + colOffset) * (bw + gap);
        buttons[i].y = startY + 3 * (bh + gap);
        buttons[i].w = 2 * bw + gap;
        buttons[i].h = bh;
      }
    }
  }

  // Scientific / Unit / RPN extra keys
  if (currentMode == MODE_SCIENTIFIC || currentMode == MODE_UNIT ||
      currentMode == MODE_RPN) {
    char *labels[4][2];
    // Fill labels based on mode
    if (currentMode == MODE_SCIENTIFIC) {
      labels[0][0] = "sin";
      labels[0][1] = "sqrt";
      labels[1][0] = "cos";
      labels[1][1] = "sqr";
      labels[2][0] = "tan";
      labels[2][1] = "x^y";
      labels[3][0] = "log";
      labels[3][1] = "ln";
    } else if (currentMode == MODE_UNIT) {
      labels[0][0] = "cm2in";
      labels[0][1] = "in2cm";
      labels[1][0] = "kg2lb";
      labels[1][1] = "lb2kg";
      labels[2][0] = "km2mi";
      labels[2][1] = "mi2km";
      labels[3][0] = "C2F";
      labels[3][1] = "F2C";
    } else { // RPN
      labels[0][0] = "SWP";
      labels[0][1] = "DRP";
      labels[1][0] = "";
      labels[1][1] = "";
      labels[2][0] = "";
      labels[2][1] = "";
      labels[3][0] = "";
      labels[3][1] = "";
    }

    for (int r = 0; r < 4; r++) {
      for (int c = 0; c < 2; c++) {
        char *lbl = labels[r][c];
        if (strlen(lbl) > 0) {
          for (int k = 0; k < numButtons; k++) {
            if (strcmp(buttons[k].label, lbl) == 0) {
              buttons[k].x = startX + c * (bw + gap);
              buttons[k].y = startY + r * (bh + gap);
              buttons[k].w = bw;
              buttons[k].h = bh;
            }
          }
        }
      }
    }
  }
}

// Theme Init
void initTheme() {
  theme_light.bg = nvgRGB(246, 247, 249);
  theme_light.display_bg = nvgRGB(246, 247, 249);
  theme_light.btn_bg_digit = nvgRGB(255, 255, 255);
  theme_light.btn_bg_op = nvgRGB(230, 240, 255); // Light Blue
  theme_light.btn_bg_action = nvgRGB(220, 220, 225);
  theme_light.text_primary = nvgRGB(50, 50, 50);
  theme_light.text_secondary = nvgRGB(100, 100, 100);
  theme_light.shadow = nvgRGBA(0, 0, 0, 30);

  theme_dark.bg = nvgRGB(14, 15, 18);
  theme_dark.display_bg = nvgRGB(14, 15, 18);
  theme_dark.btn_bg_digit = nvgRGB(30, 32, 36);
  theme_dark.btn_bg_op = nvgRGB(47, 128, 255); // Blue Accent
  theme_dark.btn_bg_action = nvgRGB(40, 42, 46);
  theme_dark.text_primary = nvgRGB(255, 255, 255);
  theme_dark.text_secondary = nvgRGB(150, 150, 150);
  theme_dark.shadow = nvgRGBA(0, 0, 0, 128);

  current_theme = &theme_dark; // Default
}

void initButtons(void) {
  initTheme();
  numButtons = 0;

  // Digits 1-9
  int nums[3][3] = {{7, 8, 9}, {4, 5, 6}, {1, 2, 3}};
  for (int row = 0; row < 3; row++) {
    for (int col = 0; col < 3; col++) {
      Button *b = &buttons[numButtons++];
      snprintf(b->label, sizeof(b->label), "%d", nums[row][col]);
      b->role = 0;
      b->color = current_theme->btn_bg_digit;
    }
  }

  // 0 button
  Button *b0 = &buttons[numButtons++];
  strcpy(b0->label, "0");
  b0->role = 0;
  b0->color = current_theme->btn_bg_digit;

  // . button
  Button *bdot = &buttons[numButtons++];
  strcpy(bdot->label, ".");
  bdot->role = 0;
  bdot->color = current_theme->btn_bg_digit;

  // = button
  Button *beq = &buttons[numButtons++];
  strcpy(beq->label, "=");
  beq->role = 1; // Op
  beq->color = current_theme->btn_bg_op;

  // Operators
  char ops[4] = {'+', '-', '*', '/'};
  for (int i = 0; i < 4; i++) {
    Button *bop = &buttons[numButtons++];
    snprintf(bop->label, sizeof(bop->label), "%c", ops[i]);
    bop->role = 1;
    bop->color = current_theme->btn_bg_op;
  }

  // Global C Button
  strcpy(cBtn.label, "C");
  cBtn.role = 2; // Action
  cBtn.color = current_theme->btn_bg_action;

  // History Toggle Button
  strcpy(histBtn.label, "H");
  histBtn.role = 2;
  histBtn.color = current_theme->btn_bg_action;

  // Draw Toggle
  strcpy(drawBtn.label, "DRAW");
  drawBtn.role = 2;
  drawBtn.color = current_theme->btn_bg_action;

  // Mode Button
  strcpy(modeBtn.label, "M");
  modeBtn.role = 2;
  modeBtn.color = current_theme->btn_bg_action;

  // Scientific
  if (numButtons < 30) {
    char *sciLabels[] = {"sin",  "cos", "tan", "log", "ln",
                         "sqrt", "sqr", "x^y", "PI",  "e"};
    for (int i = 0; i < 10; i++) {
      Button *b = &buttons[numButtons++];
      strcpy(b->label, sciLabels[i]);
      b->role = 2; // Func
      b->color = current_theme->btn_bg_action;
    }

    // Unit
    char *unitLabels[] = {"cm2in", "in2cm", "kg2lb", "lb2kg",
                          "km2mi", "mi2km", "C2F",   "F2C"};
    for (int i = 0; i < 8; i++) {
      Button *b = &buttons[numButtons++];
      strcpy(b->label, unitLabels[i]);
      b->role = 2;
      b->color = current_theme->btn_bg_action;
    }

    // RPN
    char *rpnLabels[] = {"SWP", "DRP"};
    for (int i = 0; i < 2; i++) {
      Button *b = &buttons[numButtons++];
      strcpy(b->label, rpnLabels[i]);
      b->role = 2;
      b->color = current_theme->btn_bg_action;
    }
  }

  updateLayout(232, 306);
}

// Global window pointer moved to top

void handleButtonClick(int x, int y) {
  SDL_Window *win = gWindow;
  int w, h;
  SDL_GetWindowSize(win, &w, &h);

  // History Button
  if (x >= histBtn.x && x < histBtn.x + histBtn.w && y >= histBtn.y &&
      y < histBtn.y + histBtn.h) {
    showHistory = !showHistory;
    if (showHistory)
      showDraw = 0; // Close draw panel if opening history
    SDL_SetWindowSize(win, showHistory ? w + 200 : w - 200,
                      h); // Adjust width based on current width
    updateLayout(showHistory ? w + 200 : w - 200, h); // Recalculate layout
    triggerClickAnim(2, 0);
    return;
  }

  // Mode Button
  if (x >= modeBtn.x && x < modeBtn.x + modeBtn.w && y >= modeBtn.y &&
      y < modeBtn.y + modeBtn.h) {
    isDropdownOpen = !isDropdownOpen;
    return;
  }

  // Dropdown options
  if (isDropdownOpen) {
    // 4 options: Basic, Scientific, Unit, Draw
    // Drawn from modeBtn.y + h downwards
    float rx = modeBtn.x;
    float ry = modeBtn.y;
    float rw = modeBtn.w;
    float rh = modeBtn.h;
    int itemH = 30;

    // Basic
    if (x >= rx && x < rx + 100 && y >= ry + rh && y < ry + rh + itemH) {
      currentMode = MODE_BASIC;
      isDropdownOpen = 0;
      showDraw = 0;                   // Disable draw
      SDL_SetWindowSize(win, 300, h); // Set to basic width
      updateLayout(300, h);
      return;
    }
    // Sci
    if (x >= rx && x < rx + 100 && y >= ry + rh + itemH &&
        y < ry + rh + 2 * itemH) {
      currentMode = MODE_SCIENTIFIC;
      isDropdownOpen = 0;
      showDraw = 0;
      SDL_SetWindowSize(win, 450, h); // Set to scientific width
      updateLayout(450, h);
      return;
    }
    // Unit
    if (x >= rx && x < rx + 100 && y >= ry + rh + 2 * itemH &&
        y < ry + rh + 3 * itemH) {
      currentMode = MODE_UNIT;
      isDropdownOpen = 0;
      showDraw = 0;
      SDL_SetWindowSize(win, 450, h); // Unit mode is wide
      updateLayout(450, h);
      return;
    }
    // RPN
    if (x >= rx && x < rx + 100 && y >= ry + rh + 3 * itemH &&
        y < ry + rh + 4 * itemH) {
      currentMode = MODE_RPN;
      isDropdownOpen = 0;
      showDraw = 0;
      SDL_SetWindowSize(win, 650, h);
      updateLayout(650, h);
      return;
    }
    // Draw
    if (x >= rx && x < rx + 100 && y >= ry + rh + 4 * itemH &&
        y < ry + rh + 5 * itemH) {
      currentMode = MODE_DRAW; // This handles layout logic
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

  // C Button
  if (x >= cBtn.x && x < cBtn.x + cBtn.w && y >= cBtn.y &&
      y < cBtn.y + cBtn.h) {
    calc_inputClear();
    triggerClickAnim(1, 0);
    return;
  }

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
      // SDL_Rect r = {startBtnX + i * (btnW + gap), btnY, btnW, btnH};
      int bx = startBtnX + i * (btnW + gap);
      int by = btnY;
      int bw = btnW;
      int bh = btnH;

      if (x >= bx && x < bx + bw && y >= by && y < by + bh) {
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
    Button *b = &buttons[i];
    // Only process if button is visible
    if (b->w > 0 && b->h > 0 && x >= b->x && x < b->x + b->w && y >= b->y &&
        y < b->y + b->h) {
      char *label = b->label;
      if ((label[0] >= '0' && label[0] <= '9') || label[0] == '.') {
        calc_inputDigit(label);
      } else if (label[0] == '+' || label[0] == '-' || label[0] == '*' ||
                 label[0] == '/' || label[0] == '^') {
        calc_inputOperator(label[0]);
      } else if (strcmp(label, "=") == 0) { // Basic mode equals
        if (currentMode == MODE_RPN) {
          calc_inputRPN("ENT");
        } else {
          calc_inputEquals();
        }
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

// Hover state helper
void ui_handle_mouse_move(int x, int y) {
  for (int i = 0; i < numButtons; i++) {
    Button *b = &buttons[i];
    if (x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h) {
      b->is_hovered = 1;
    } else {
      b->is_hovered = 0;
    }
  }
  // Check globals
  Button *globals[] = {&modeBtn, &histBtn, &cBtn};
  for (int i = 0; i < 3; i++) {
    Button *b = globals[i];
    if (x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h) {
      b->is_hovered = 1;
    } else {
      b->is_hovered = 0;
    }
  }
}

// Init NVG
void ui_init_nanovg(void) {
  // flags: NVG_ANTIALIAS | NVG_STENCIL_STROKES | NVG_DEBUG
  vg = nvgCreateGL3(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
  if (vg == NULL) {
    printf("Could not init nanovg.\n");
    exit(1);
  }
  // Load fonts if available, or fail gracefully?
  // Basic font loading - assuming generic font/file present or skip text
  // If no font file, nvgText won't draw.
  // I'll try to load a system font or skip.
  // Mac: /System/Library/Fonts/Helvetica.ttc
  fontNormal = nvgCreateFont(vg, "sans", "/System/Library/Fonts/Helvetica.ttc");
  if (fontNormal == -1) {
    printf("Could not find font. Text will be missing.\n");
  }
  fontBold = nvgCreateFont(
      vg, "sans-bold", "/System/Library/Fonts/Helvetica.ttc"); // Just re-use
}

// Drawing helpers
void draw_rrect_shadow(NVGcontext *vg, float x, float y, float w, float h,
                       float rad, NVGcolor bg, NVGcolor shadow) {
  // Shadow
  NVGpaint shadowPaint =
      nvgBoxGradient(vg, x, y + 2, w, h, rad, 10, shadow, nvgRGBA(0, 0, 0, 0));
  nvgBeginPath(vg);
  nvgRect(vg, x - 10, y - 10, w + 20, h + 20);
  nvgRoundedRect(vg, x, y, w, h, rad);
  nvgPathWinding(vg, NVG_HOLE);
  nvgFillPaint(vg, shadowPaint);
  nvgFill(vg);

  // Body
  nvgBeginPath(vg);
  nvgRoundedRect(vg, x, y, w, h, rad);
  nvgFillColor(vg, bg);
  nvgFill(vg);
  nvgFill(vg);
}

void draw_crash_screen(NVGcontext *vg, int w, int h) {
  // Fake Blue/Black Screen
  nvgBeginPath(vg);
  nvgRect(vg, 0, 0, w, h);
  nvgFillColor(vg, nvgRGB(0, 0, 150)); // BSOD Blue
  nvgFill(vg);

  nvgFillColor(vg, nvgRGB(255, 255, 255));
  nvgFontSize(vg, 40);
  nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
  nvgText(vg, w / 2, h / 2 - 40, ":(", NULL);

  nvgFontSize(vg, 20);
  nvgText(vg, w / 2, h / 2 + 20, "Your PC ran into a problem", NULL);
  nvgText(vg, w / 2, h / 2 + 45, "and needs to restart.", NULL);

  nvgFontSize(vg, 16);
  nvgText(vg, w / 2, h / 2 + 80, "Error: DIVIDE_BY_ZERO", NULL);
}

// Helper to draw a single button
void draw_button_render(NVGcontext *vg, Button *b, float dt) {
  if (b->w <= 0)
    return;

  // Anim
  if (b->is_hovered) {
    b->anim_t += dt * 5.0f;
    if (b->anim_t > 1.0f)
      b->anim_t = 1.0f;
  } else {
    b->anim_t -= dt * 5.0f;
    if (b->anim_t < 0.0f)
      b->anim_t = 0.0f;
  }

  NVGcolor c = b->color;
  if (b->role == 1)
    c = current_theme->btn_bg_op;
  // Brighten on hover
  if (b->anim_t > 0) {
    c.r += b->anim_t * 0.1f;
    c.g += b->anim_t * 0.1f;
    c.b += b->anim_t * 0.1f;
  }

  draw_rrect_shadow(vg, b->x, b->y, b->w, b->h, 14, c, current_theme->shadow);

  nvgFillColor(vg, current_theme->text_primary);
  if (b->role == 1 && current_theme == &theme_dark)
    nvgFillColor(vg, nvgRGB(255, 255, 255));

  // Dynamic font size relative to button height
  float fsize = b->h * 0.5f;
  if (fsize < 18)
    fsize = 18;
  if (fsize > 40)
    fsize = 40;

  nvgFontSize(vg, fsize);
  nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
  nvgText(vg, b->x + b->w / 2, b->y + b->h / 2, b->label, NULL);
}

void ui_render(SDL_Window *win) {
  int w, h;
  SDL_GetWindowSize(win, &w, &h);
  int winWidth, winHeight;
  SDL_GL_GetDrawableSize(win, &winWidth, &winHeight); // For high-dpi
  float pxRatio = (float)winWidth / (float)w;

  static int lastW = 0, lastH = 0;
  if (w != lastW || h != lastH) {
    updateLayout(w, h); // Layout uses logical points
    lastW = w;
    lastH = h;
  }

  nvgBeginFrame(vg, w, h, pxRatio);

  // Background
  nvgBeginPath(vg);
  nvgRect(vg, 0, 0, w, h);
  nvgFillColor(vg, current_theme->bg);
  nvgFill(vg);

  // Sidebar
  if (showHistory || currentMode == MODE_RPN) {
    nvgBeginPath(vg);
    nvgRect(vg, w - 200, 0, 200, h);
    nvgFillColor(vg,
                 nvgRGB(40, 40, 40)); // keep distinct sidebar color or themed?
    nvgFill(vg);

    // History items or RPN stack
    // ... (Simplified text rendering)
    nvgFillColor(vg, nvgRGB(200, 200, 200));
    nvgFontSize(vg, 16);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);

    if (currentMode == MODE_RPN) {
      for (int i = 0; i < 4; i++) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%d: %.5g", i, calc.stack[i]);
        nvgText(vg, w - 190, h - 100 - i * 50, buf, NULL);
      }
    } else {
      for (int i = 0; i < historyCount; i++) {
        nvgText(vg, w - 190, 30 + i * 40, history[i].equation, NULL);
        char res[64];
        snprintf(res, sizeof(res), "= %.5g", history[i].result);
        nvgText(vg, w - 190, 50 + i * 40, res, NULL);
      }
    }
  }

  // Display
  // Draw Display BG
  nvgBeginPath(vg);
  nvgRoundedRect(vg, displayX, displayY, displayW, displayH, 10);
  nvgFillColor(vg, current_theme->display_bg);
  nvgFill(vg);
  // Inner Shadow?

  // Display Text
  char formattedText[64];
  formatNumber(calc.display, formattedText, sizeof(formattedText));
  nvgFillColor(vg, current_theme->text_primary);

  // Dynamic display font
  float dispFont = h * 0.08f;
  if (dispFont < 36)
    dispFont = 36;
  if (dispFont > 80)
    dispFont = 80;

  nvgFontSize(vg, dispFont);
  nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
  nvgText(vg, displayX + displayW - 10, displayY + 25, formattedText, NULL);

  // Buttons
  float dt = 0.016f; // Approx

  // Update labels dynamic
  for (int i = 0; i < numButtons; i++) {
    if (strcmp(buttons[i].label, "=") == 0 ||
        strcmp(buttons[i].label, "ENT") == 0) {
      if (currentMode == MODE_RPN)
        strcpy(buttons[i].label, "ENT");
      else
        strcpy(buttons[i].label, "=");
    }
  }

  // Draw Global Control Buttons
  draw_button_render(vg, &modeBtn, dt);
  draw_button_render(vg, &histBtn, dt);
  draw_button_render(vg, &cBtn, dt);

  for (int i = 0; i < numButtons; i++) {
    draw_button_render(vg, &buttons[i], dt);
  }

  // Active Dropdown
  if (isDropdownOpen) {
    float rx = modeBtn.x;
    float ry = modeBtn.y + modeBtn.h + 5;
    float Rw = 120, Rh = 150;
    // Shadow
    draw_rrect_shadow(vg, rx, ry, Rw, Rh, 5, nvgRGB(50, 50, 50),
                      nvgRGBA(0, 0, 0, 100));

    nvgFillColor(vg, nvgRGB(255, 255, 255));
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgText(vg, rx + 10, ry + 20, "Basic", NULL);
    nvgText(vg, rx + 10, ry + 50, "Scientific", NULL);
    nvgText(vg, rx + 10, ry + 80, "Unit", NULL);
    nvgText(vg, rx + 10, ry + 110, "RPN", NULL);
    nvgText(vg, rx + 10, ry + 140, "Draw", NULL);
  }

  // Draw Grid Overlay
  if (showDraw || currentMode == MODE_DRAW) {
    // ... Reuse existing grid logic but draw with NVG
    // For brevity, skipping full grid redraw migration details here,
    // assuming it's covered or I can add it if needed.
    // Will render a simple placeholder for grid

    // Actually I should render it.
    int calcWidth = showHistory ? w - 200 : w;
    int gridStartY = 120;
    int padH = h - gridStartY - 70;
    int padW = calcWidth - 40;
    if (padH < 100)
      padH = 100;
    int cellSize = (padW < padH ? padW : padH) / 28;
    int gridSize = 28 * cellSize;
    int gridX = 20 + (padW - gridSize) / 2;
    int gridY = gridStartY + (padH - gridSize) / 2;

    nvgBeginPath(vg);
    nvgRect(vg, gridX - 2, gridY - 2, gridSize + 4, gridSize + 4);
    nvgStrokeColor(vg, nvgRGB(100, 100, 100));
    nvgStroke(vg);

    for (int r = 0; r < 28; r++) {
      for (int c = 0; c < 28; c++) {
        if (drawGrid[r][c]) {
          nvgBeginPath(vg);
          nvgRect(vg, gridX + c * cellSize, gridY + r * cellSize, cellSize,
                  cellSize);
          nvgFillColor(vg, nvgRGB(50, 50, 50));
          nvgFill(vg);
        }
      }
    }

    // Draw Mode Buttons
    int numDrBtns = 5;
    char *drLabels[] = {"+", "-", "*", "/", "CLR"};
    int gap = 10;
    int totalGap = (numDrBtns - 1) * gap;
    int availW = padW;
    int btnW = (availW - totalGap) / numDrBtns;
    int btnH = 30;
    int btnY = gridStartY + padH + 10;
    int startBtnX = 20;

    for (int i = 0; i < numDrBtns; i++) {
      Button b;
      // Make a temp button to render
      b.x = startBtnX + i * (btnW + gap);
      b.y = btnY;
      b.w = btnW;
      b.h = btnH;
      strcpy(b.label, drLabels[i]);
      b.role = (strcmp(drLabels[i], "CLR") == 0) ? 2 : 1;
      b.is_hovered = 0; // Don't track hover for temp buttons perfectly yet or
                        // use mouse pos
      // Check hover manually
      int mouseX, mouseY;
      SDL_GetMouseState(&mouseX, &mouseY);
      float pxRatioInv = 1.0f; // Simplified, assuming logical coords match
      if (mouseX >= b.x && mouseX < b.x + b.w && mouseY >= b.y &&
          mouseY < b.y + b.h) {
        b.is_hovered = 1;
      }
      b.anim_t = b.is_hovered ? 1.0f : 0.0f;

      if (b.role == 1)
        b.color = current_theme->btn_bg_op;
      else
        b.color = current_theme->btn_bg_action;

      draw_button_render(vg, &b, dt);
    }
  }

  // Crash Mask
  if (isCrashMode) {
    draw_crash_screen(vg, w, h);
  }

  nvgEndFrame(vg);
  SDL_GL_SwapWindow(win);
}

void set_macos_window_style(SDL_Window *window);

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;

  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    printf("SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
    return 1;
  }

  // OpenGL 3.3 Core
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8); // Required for NanoVG

  SDL_Window *win = SDL_CreateWindow(
      "jai's calculator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 232,
      306, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);

  if (win) {
    SDL_SetWindowMinimumSize(win, 220, 300);
  }

  if (!win) {
    printf("Window could not be created! SDL_Error: %s\n", SDL_GetError());
    return 1;
  }

  gWindow = win;

  SDL_GLContext glContext = SDL_GL_CreateContext(win);
  if (!glContext) {
    printf("Could not create GL context! SDL_Error: %s\n", SDL_GetError());
    return 1;
  }
  SDL_GL_MakeCurrent(win, glContext);

  // Apply Mac Style
  set_macos_window_style(win);

  // VSync
  SDL_GL_SetSwapInterval(1);

  // Init UI (NanoVG)
  ui_init_nanovg();

  // Load ML Model
  if (model_load("model.bin", &nn)) {
    printf("Successfully loaded model.bin\n");
    modelLoaded = 1;
  } else {
    // printf("Failed to load model.bin! Make sure to run ./train first.\n");
  }

  // Load State
  load_state();

  initButtons();

  int quit = 0;
  SDL_Event e;

  while (!quit) {
    int mouseX, mouseY;
    SDL_GetMouseState(&mouseX, &mouseY);
    ui_handle_mouse_move(mouseX,
                         mouseY); // Update hover states every frame or on
                                  // mouse event? calling here is safe.

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
        } else if (predictedDigit >= 0 && predictedDigit <= 9) {
          // Insert predicted digit into calculator display
          char digit[2] = {'0' + predictedDigit, '\0'};
          calc_inputDigit(digit);

          FILE *f = fopen("debug.log", "a");
          if (f) {
            fprintf(f, "Prediction: %d (Valid)\n", predictedDigit);
            fclose(f);
          }

        } else {
          printf("Ignored invalid prediction: %d\n", predictedDigit);
          FILE *f = fopen("debug.log", "a");
          if (f) {
            fprintf(f, "Prediction: %d (INVALID)\n", predictedDigit);
            fclose(f);
          }
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

    ui_render(win);
    // SDL_Delay(16); // VSync handles waiting usually, but small delay is ok
  }

  save_state();

  // Cleanup
  // nvgDeleteGL3(vg); // assuming this function name
  SDL_GL_DeleteContext(glContext);
  SDL_DestroyWindow(win);
  SDL_Quit();
  return 0;
}

// Persistence Implementation
void save_state() {
  FILE *f = fopen("calc_state.dat", "wb");
  if (!f)
    return;

  // Save Calculator State
  fwrite(&calc, sizeof(Calculator), 1, f);

  // Save History
  fwrite(&historyCount, sizeof(int), 1, f);
  fwrite(history, sizeof(HistoryEntry), 8, f);

  fclose(f);
}

void load_state() {
  FILE *f = fopen("calc_state.dat", "rb");
  if (!f)
    return;

  fread(&calc, sizeof(Calculator), 1, f);

  fread(&historyCount, sizeof(int), 1, f);
  if (historyCount > 8)
    historyCount = 8;
  fread(history, sizeof(HistoryEntry), 8, f);

  fclose(f);
}
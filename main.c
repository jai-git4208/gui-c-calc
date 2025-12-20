#include "model.h"
#include "nanovg.h"
#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#define GL_GLEXT_PROTOTYPES
#include <SDL2/SDL_opengl.h>
#endif
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define NANOVG_GL3_IMPLEMENTATION
#include "nanovg_gl.h"
typedef struct {
  char display[32];
  double storedValue;
  char pendingOp;
  int hasPendingOp;
  int clearOnNextDigit;

  double stack[4];
  int stackSize;
} Calculator;
typedef struct {
  float x, y, w, h;
  char label[16];
  int role;
  int is_hovered;
  int is_pressed;
  float anim_t;
  NVGcolor color;
} Button;
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

SDL_Color COLOR_BG = {30, 30, 30, 255};
SDL_Color _COLOR_DIGIT = {70, 70, 70, 255};
SDL_Color _COLOR_OP = {255, 149, 0, 255};
SDL_Color _COLOR_CLEAR = {165, 165, 165, 255};
SDL_Color COLOR_TEXT = {255, 255, 255, 255};
SDL_Color COLOR_DISPLAY = {45, 45, 45, 255};
Calculator calc = {"0", 0, 0, 0, 0, {0, 0, 0, 0}, 0};
Button buttons[40];
int numButtons = 0;
int divZeroCount = 0;
int isCrashMode = 0;
Uint32 crashStartTime = 0;
int is404Mode = 0;
Uint32 easterEggStart = 0;
int isHeartAnimActive = 0;

Uint32 equalsPressTime = 0;
int isEqualsDown = 0;
char inputSequence[16] = "";
int isPrimeResult = 0;
char specialMessage[32] = "";
int isDevMode = 0;
Uint32 frameCount = 0;
float fps = 0.0f;
Uint32 lastFPSUpdate = 0;
double lastResult = 0;
int equalsCount = 0;
int numberSequence[10];
int seqIndex = 0;
#define KONAMI_LENGTH 10
int konamiSequence[KONAMI_LENGTH];
int konamiIndex = 0;
int isRainbowMode = 0;
float rainbowHue = 0.0f;
SDL_Window *gWindow = NULL;
TTF_Font *gFont = NULL;
TTF_Font *gFontLarge = NULL;
int winWidth = 232;
int winHeight = 306;
int isWindowDragging = 0;
int dragOffsetX = 0;
int dragOffsetY = 0;
typedef enum {
  MODE_BASIC,
  MODE_DRAW,
  MODE_SCIENTIFIC,
  MODE_UNIT,
  MODE_RPN,
  MODE_GRAPH
} CalculatorMode;
CalculatorMode currentMode = MODE_BASIC;
int showHistory = 0;
int isDropdownOpen = 0;
Button cBtn;

Button modeBtn;
int clickAnimType = -1;
int clickAnimIdx = -1;
Uint32 clickAnimTime = 0;

// Graphing state
char graphEq[128] = "";
float xMin = -10.0f, xMax = 10.0f;
float yMin = -5.0f, yMax = 5.0f;
int isSidebarExpanded = 1;
float graphAreaX, graphAreaY, graphAreaW, graphAreaH;
float sidebarX, sidebarY, sidebarW, sidebarH;
float keypadX, keypadY, keypadW, keypadH;
Button graphButtons[48];
int numGraphButtons = 0;
int graphKeypadPage = 0; // 0: NUM, 1: ABC, 2: FUNC

void triggerClickAnim(int type, int idx) {
  clickAnimType = type;
  clickAnimIdx = idx;
  clickAnimTime = SDL_GetTicks();
}

void calc_inputEquals(void);
void save_state(void);
void load_state(void);
int isPrime(double val) {
  if (val != floor(val))
    return 0;
  long n = (long)val;
  if (n <= 1)
    return 0;
  if (n <= 3)
    return 1;
  if (n % 2 == 0 || n % 3 == 0)
    return 0;
  for (long i = 5; i * i <= n; i += 6) {
    if (n % i == 0 || n % (i + 2) == 0)
      return 0;
  }
  return 1;
}
double signum(double x) { return (x > 0) - (x < 0); }

// Expression Parser for Graphing
typedef struct {
  const char *ptr;
} GraphParser;
double parse_graph_expr(GraphParser *p, double xVal);
double parse_graph_factor(GraphParser *p, double xVal) {
  while (isspace(*p->ptr))
    p->ptr++;
  if (*p->ptr == '(') {
    p->ptr++;
    double val = parse_graph_expr(p, xVal);
    if (*p->ptr == ')')
      p->ptr++;
    return val;
  }
  if (*p->ptr == '-') {
    p->ptr++;
    return -parse_graph_factor(p, xVal);
  }
  if (*p->ptr == '+') {
    p->ptr++;
    return parse_graph_factor(p, xVal);
  }
  if (isalpha(*p->ptr) && !isalpha(*(p->ptr + 1)) && *(p->ptr + 1) != '(') {
    // Single letter variable
    p->ptr++;
    return xVal;
  }
  if (strncmp(p->ptr, "pi", 2) == 0 && !isalpha(*(p->ptr + 2))) {
    p->ptr += 2;
    return M_PI;
  }
  if (strncmp(p->ptr, "abs", 3) == 0) {
    p->ptr += 3;
    return fabs(parse_graph_factor(p, xVal));
  }
  if (strncmp(p->ptr, "sign", 4) == 0) {
    p->ptr += 4;
    return signum(parse_graph_factor(p, xVal));
  }
  if (strncmp(p->ptr, "floor", 5) == 0) {
    p->ptr += 5;
    return floor(parse_graph_factor(p, xVal));
  }
  if (strncmp(p->ptr, "ceil", 4) == 0) {
    p->ptr += 4;
    return ceil(parse_graph_factor(p, xVal));
  }
  if (strncmp(p->ptr, "sqrt", 4) == 0) {
    p->ptr += 4;
    return sqrt(parse_graph_factor(p, xVal));
  }
  if (strncmp(p->ptr, "sinh", 4) == 0) {
    p->ptr += 4;
    return sinh(parse_graph_factor(p, xVal));
  }
  if (strncmp(p->ptr, "cosh", 4) == 0) {
    p->ptr += 4;
    return cosh(parse_graph_factor(p, xVal));
  }
  if (strncmp(p->ptr, "tanh", 4) == 0) {
    p->ptr += 4;
    return tanh(parse_graph_factor(p, xVal));
  }
  if (strncmp(p->ptr, "mod", 3) == 0) {
    p->ptr += 3;
    while (isspace(*p->ptr))
      p->ptr++;
    if (*p->ptr == '(') {
      p->ptr++;
      double a = parse_graph_expr(p, xVal);
      while (isspace(*p->ptr))
        p->ptr++;
      if (*p->ptr == ',') {
        p->ptr++;
        double b = parse_graph_expr(p, xVal);
        while (isspace(*p->ptr))
          p->ptr++;
        if (*p->ptr == ')')
          p->ptr++;
        return fmod(a, b);
      }
    }
    return 0;
  }
  if (strncmp(p->ptr, "sin", 3) == 0) {
    p->ptr += 3;
    return sin(parse_graph_factor(p, xVal));
  }
  if (strncmp(p->ptr, "cos", 3) == 0) {
    p->ptr += 3;
    return cos(parse_graph_factor(p, xVal));
  }
  if (strncmp(p->ptr, "tan", 3) == 0) {
    p->ptr += 3;
    return tan(parse_graph_factor(p, xVal));
  }
  if (strncmp(p->ptr, "asin", 4) == 0) {
    p->ptr += 4;
    return asin(parse_graph_factor(p, xVal));
  }
  if (strncmp(p->ptr, "acos", 4) == 0) {
    p->ptr += 4;
    return acos(parse_graph_factor(p, xVal));
  }
  if (strncmp(p->ptr, "atan", 4) == 0) {
    p->ptr += 4;
    return atan(parse_graph_factor(p, xVal));
  }
  if (strncmp(p->ptr, "log", 3) == 0) {
    p->ptr += 3;
    return log10(parse_graph_factor(p, xVal));
  }
  if (strncmp(p->ptr, "ln", 2) == 0) {
    p->ptr += 2;
    return log(parse_graph_factor(p, xVal));
  }
  if (isdigit(*p->ptr) || *p->ptr == '.') {
    char *end;
    double val = strtod(p->ptr, &end);
    p->ptr = end;
    return val;
  }
  return 0;
}
double parse_graph_term(GraphParser *p, double xVal) {
  double val = parse_graph_factor(p, xVal);
  while (1) {
    while (isspace(*p->ptr))
      p->ptr++;
    if (*p->ptr == '*' || *p->ptr == '/') {
      char op = *p->ptr++;
      double next = parse_graph_factor(p, xVal);
      if (op == '*')
        val *= next;
      else
        val = (next != 0) ? val / next : 0;
    } else if (*p->ptr == '^') {
      p->ptr++;
      double next = parse_graph_factor(p, xVal);
      val = pow(val, next);
    } else if (*p->ptr == '(' || *p->ptr == 'x' || isdigit(*p->ptr) ||
               (isalpha(*p->ptr) && strncmp(p->ptr, "pi", 2) != 0)) {
      // Implicit multiplication
      val *= parse_graph_factor(p, xVal);
    } else if (strncmp(p->ptr, "pi", 2) == 0) {
      val *= parse_graph_factor(p, xVal);
    } else {
      break;
    }
  }
  return val;
}
double parse_graph_expr(GraphParser *p, double xVal) {
  double val = parse_graph_term(p, xVal);
  while (isspace(*p->ptr))
    p->ptr++;
  while (*p->ptr == '+' || *p->ptr == '-') {
    char op = *p->ptr++;
    double next = parse_graph_term(p, xVal);
    if (op == '+')
      val += next;
    else if (op == '-')
      val -= next;
    while (isspace(*p->ptr))
      p->ptr++;
  }
  return val;
}
double parse_graph_comparison(GraphParser *p, double xVal) {
  double val = parse_graph_expr(p, xVal);
  while (1) {
    while (isspace(*p->ptr))
      p->ptr++;
    if (strncmp(p->ptr, "<=", 2) == 0) {
      p->ptr += 2;
      val = (val <= parse_graph_expr(p, xVal));
    } else if (strncmp(p->ptr, ">=", 2) == 0) {
      p->ptr += 2;
      val = (val >= parse_graph_expr(p, xVal));
    } else if (strncmp(p->ptr, "==", 2) == 0) {
      p->ptr += 2;
      val = (val == parse_graph_expr(p, xVal));
    } else if (strncmp(p->ptr, "!=", 2) == 0) {
      p->ptr += 2;
      val = (val != parse_graph_expr(p, xVal));
    } else if (*p->ptr == '<') {
      p->ptr++;
      val = (val < parse_graph_expr(p, xVal));
    } else if (*p->ptr == '>') {
      p->ptr++;
      val = (val > parse_graph_expr(p, xVal));
    } else {
      break;
    }
  }
  return val;
}
double evaluate_graph(const char *expr, double xVal) {
  if (!expr || strlen(expr) == 0)
    return 0;

  const char *actualExpr = expr;
  // Skip "y =" or "y<" or "y>"
  while (isspace(*actualExpr))
    actualExpr++;
  if (*actualExpr == 'y') {
    const char *next = actualExpr + 1;
    while (isspace(*next))
      next++;
    if (*next == '=' || *next == '<' || *next == '>') {
      if (*next == '=' && *(next + 1) == '=') {
        // equality, don't skip
      } else {
        actualExpr = next;
        if (*actualExpr == '=')
          actualExpr++;
        else if (*actualExpr == '<' || *actualExpr == '>') {
          actualExpr++;
          if (*actualExpr == '=')
            actualExpr++;
        }
      }
    }
  } else {
    // Legacy support for skipping everything before last '='
    const char *lastEq = NULL;
    const char *curr = expr;
    while (*curr) {
      if (*curr == '=' &&
          (curr == expr || (*(curr - 1) != '<' && *(curr - 1) != '>' &&
                            *(curr - 1) != '!' && *(curr + 1) != '='))) {
        lastEq = curr;
      }
      curr++;
    }
    if (lastEq)
      actualExpr = lastEq + 1;
  }

  while (isspace(*actualExpr))
    actualExpr++;

  if (!*actualExpr)
    return 0;

  GraphParser p = {actualExpr};
  return parse_graph_comparison(&p, xVal);
}

void recordInput(const char *key) {

  size_t len = strlen(inputSequence);
  size_t keyLen = strlen(key);

  if (len + keyLen >= sizeof(inputSequence) - 1) {

    size_t shift = (len + keyLen) - (sizeof(inputSequence) - 2);

    if (shift < len) {
      memmove(inputSequence, inputSequence + shift, len - shift + 1);
    } else {
      inputSequence[0] = '\0';
    }
  }
  strcat(inputSequence, key);

  char *found = strstr(inputSequence, "CC==");
  if (found) {
    snprintf(specialMessage, sizeof(specialMessage), "HELLO :)");
    inputSequence[0] = '\0';
  }
}
NVGcolor hsvToRgb(float h, float s, float v) {
  float c = v * s;
  float x = c * (1 - fabs(fmod(h / 60.0f, 2) - 1));
  float m = v - c;
  float r, g, b;

  if (h < 60) {
    r = c;
    g = x;
    b = 0;
  } else if (h < 120) {
    r = x;
    g = c;
    b = 0;
  } else if (h < 180) {
    r = 0;
    g = c;
    b = x;
  } else if (h < 240) {
    r = 0;
    g = x;
    b = c;
  } else if (h < 300) {
    r = x;
    g = 0;
    b = c;
  } else {
    r = c;
    g = 0;
    b = x;
  }

  return nvgRGBf((r + m), (g + m), (b + m));
}

void calc_inputDigit(const char *digit) {
  if (strcmp(digit, ".") == 0) {

    if (strchr(calc.display, '.') != NULL) {

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
    isPrimeResult = 0;
  } else if (strlen(calc.display) < 15) {
    strcat(calc.display, digit);
    isPrimeResult = 0;
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
void calc_stackPush(double val) {

  calc.stack[3] = calc.stack[2];
  calc.stack[2] = calc.stack[1];
  calc.stack[1] = calc.stack[0];
  calc.stack[0] = val;
}

double calc_stackPop() {
  double val = calc.stack[0];

  calc.stack[0] = calc.stack[1];
  calc.stack[1] = calc.stack[2];
  calc.stack[2] = calc.stack[3];

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

    snprintf(calc.display, sizeof(calc.display), "%.10g", calc.stack[0]);
  } else if (strcmp(op, "DRP") == 0) {
    calc_stackPop();
    snprintf(calc.display, sizeof(calc.display), "%.10g", calc.stack[0]);
  } else if (strcmp(op, "CLR") == 0) {

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
void calc_inputOperator(char op) {
  if (currentMode == MODE_RPN) {

    double x = atof(calc.display);

    double y = calc_stackPop();

    double res = 0;
    if (op == '+')
      res = y + x;
    else if (op == '-')
      res = y - x;
    else if (op == '*')
      res = y * x;
    else if (op == '/')
      res = (x != 0) ? y / x : 0;
    else if (op == '^')
      res = pow(y, x);

    calc_stackPush(res);
    snprintf(calc.display, sizeof(calc.display), "%.10g", res);
    calc.clearOnNextDigit = 1;
    return;
  }

  if (calc.hasPendingOp) {
    calc_inputEquals();
  }
  calc.storedValue = atof(calc.display);
  calc.pendingOp = op;
  calc.hasPendingOp = 1;
  strcpy(calc.display, "0");
}
typedef struct {
  char equation[64];
  double result;
} HistoryEntry;

HistoryEntry history[8];
int historyCount = 0;

void addToHistory(const char *opA, char op, const char *opB, double result) {

  if (historyCount == 8) {
    for (int i = 0; i < 7; i++) {
      history[i] = history[i + 1];
    }
    historyCount = 7;
  }

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
      result = 0;
      if (divZeroCount >= 1) {
        isCrashMode = 1;
        crashStartTime = SDL_GetTicks();
        SDL_SetWindowFullscreen(gWindow, SDL_WINDOW_FULLSCREEN_DESKTOP);
      }
    } else {
      divZeroCount = 0;
      result = calc.storedValue / current;
    }
    break;
  case '^':
    result = pow(calc.storedValue, current);
    break;
  }

  char opA[32];
  snprintf(opA, sizeof(opA), "%g", calc.storedValue);
  addToHistory(opA, calc.pendingOp, calc.display, result);
  save_state();

  snprintf(calc.display, sizeof(calc.display), "%g", result);
  calc.hasPendingOp = 0;
  calc.clearOnNextDigit = 1;

  isPrimeResult = isPrime(result);

  if (fabs(result - 404) < 1e-9) {
    is404Mode = 1;
  }

  if (fabs(result - 42) < 1e-9) {
    isDevMode = !isDevMode;
  }

  if (fabs(lastResult - 9) < 1e-9 && calc.hasPendingOp == 0) {
    equalsCount++;
    if (equalsCount >= 2) {
      snprintf(specialMessage, sizeof(specialMessage), "wake up, neo");
      equalsCount = 0;
    }
  } else {
    equalsCount = 0;
  }

  lastResult = result;
}

void calc_inputClear(void) {
  strcpy(calc.display, "0");
  calc.storedValue = 0;
  calc.pendingOp = 0;
  calc.hasPendingOp = 0;
  calc.clearOnNextDigit = 0;
  isCrashMode = 0;
  is404Mode = 0;

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
void formatNumber(const char *src, char *dest, size_t destSize) {
  const char *dot = strchr(src, '.');
  int integerLen = dot ? (int)(dot - src) : (int)strlen(src);
  int isNegative = (src[0] == '-');

  char intPart[64];
  int intIdx = 0;

  int srcIdx = integerLen - 1;
  int digitCount = 0;

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

  int destIdx = 0;
  for (int i = intIdx - 1; i >= 0 && destIdx < destSize - 1; i--) {
    dest[destIdx++] = intPart[i];
  }

  if (dot) {
    int i = 0;
    while (dot[i] && destIdx < destSize - 1) {
      dest[destIdx++] = dot[i++];
    }
  }

  dest[destIdx] = '\0';
}
Button histBtn;
int showDraw = 0;
Button drawBtn;
unsigned char drawGrid[28][28] = {0};
int isDrawing = 0;
NeuralNetwork nn;
int modelLoaded = 0;
int predictedDigit = -1;
Uint32 lastDrawTime = 0;
int hasDrawnSomething = 0;
#define AUTO_PREDICT_DELAY 800
int predictDigit(void) {
  if (!modelLoaded) {
    printf("Model not loaded, cannot predict.\n");
    return -1;
  }

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

  if (maxR == -1)
    return -1;

  int w = maxC - minC + 1;
  int h = maxR - minR + 1;

  float scale = 20.0f / (w > h ? w : h);

  float scaled20[20][20];
  memset(scaled20, 0, sizeof(scaled20));

  int targetW = (int)(w * scale);
  int targetH = (int)(h * scale);

  int offR = (20 - targetH) / 2;
  int offC = (20 - targetW) / 2;

  for (int r = 0; r < 20; r++) {
    for (int c = 0; c < 20; c++) {

      if (r < offR || r >= offR + targetH || c < offC || c >= offC + targetW) {
        scaled20[r][c] = 0.0f;
        continue;
      }

      float srcR = (r - offR) / scale + minR;
      float srcC = (c - offC) / scale + minC;

      int r0 = (int)srcR;
      int c0 = (int)srcC;

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

        scaled20[r][c] = (float)drawGrid[r0][c0];
      }
    }
  }

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

  float input[784];
  for (int i = 0; i < 28; i++) {
    for (int j = 0; j < 28; j++) {
      input[i * 28 + j] = processed[i][j];
    }
  }

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

    if (iou > 0.4) {
      return -2;
    }
  }

  return model_predict(&nn, input);
}
float displayX, displayY, displayW, displayH;

void updateLayout(int width, int height) {
  winWidth = width;
  winHeight = height;

  if (currentMode == MODE_GRAPH) {
    sidebarW = isSidebarExpanded ? 200 : 0;
    sidebarX = 0;
    sidebarY = 0;
    sidebarH = height - 120;

    keypadW = width;
    keypadH = 120;
    keypadX = 0;
    keypadY = height - keypadH;

    graphAreaX = sidebarW;
    graphAreaY = 0;
    graphAreaW = width - sidebarW;
    graphAreaH = height - keypadH;

    // Position mode button in graph mode (top left of graph area or sidebar)
    modeBtn.x = sidebarW + 10;
    modeBtn.y = 10;
    modeBtn.w = 40;
    modeBtn.h = 30;

    // Layout graph keypad buttons
    int gRows = 4;
    int gCols = 12;
    float gBtnW = (float)keypadW / gCols;
    float gBtnH = (float)keypadH / gRows;

    for (int i = 0; i < numGraphButtons; i++) {
      int r = i / gCols;
      int c = i % gCols;
      graphButtons[i].x = keypadX + c * gBtnW + 2;
      graphButtons[i].y = keypadY + r * gBtnH + 2;
      graphButtons[i].w = gBtnW - 4;
      graphButtons[i].h = gBtnH - 4;
    }
    return;
  }

  if (currentMode == MODE_SCIENTIFIC || currentMode == MODE_UNIT ||
      currentMode == MODE_RPN) {

  } else {
  }

  int controlY = 80;
  int controlH = 30;
  int startY = 120;

  int calcWidth = width - ((showHistory || currentMode == MODE_RPN) ? 200 : 0);
  int padW = calcWidth - 40;

  displayX = 20;
  displayY = 20;
  displayW = calcWidth - 40;
  displayH = 50;

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

  int cols = (currentMode == MODE_SCIENTIFIC || currentMode == MODE_UNIT ||
              currentMode == MODE_RPN)
                 ? 6
                 : 4;
  float bw = (float)(padW - gap * (cols - 1)) / cols;

  int padH = height - startY - 20;

  if (padH < 50)
    padH = 50;
  float bh = (float)(padH - 3 * gap) / 4;

  int colOffset = (currentMode == MODE_SCIENTIFIC || currentMode == MODE_UNIT ||
                   currentMode == MODE_RPN)
                      ? 2
                      : 0;
  int startX = 20;

  if (currentMode != MODE_DRAW && !showDraw) {
    for (int i = 0; i < numButtons; i++) {
      int r = -1, c = -1;
      char *l = buttons[i].label;

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

      if (strcmp(l, "0") == 0) {
        buttons[i].x = startX + (0 + colOffset) * (bw + gap);
        buttons[i].y = startY + 3 * (bh + gap);
        buttons[i].w = 2 * bw + gap;
        buttons[i].h = bh;
      }
    }
  }

  if (currentMode == MODE_SCIENTIFIC || currentMode == MODE_UNIT ||
      currentMode == MODE_RPN) {
    char *labels[4][2];

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
    } else {
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
void initTheme() {
  theme_light.bg = nvgRGB(246, 247, 249);
  theme_light.display_bg = nvgRGB(246, 247, 249);
  theme_light.btn_bg_digit = nvgRGB(255, 255, 255);
  theme_light.btn_bg_op = nvgRGB(230, 240, 255);
  theme_light.btn_bg_action = nvgRGB(220, 220, 225);
  theme_light.text_primary = nvgRGB(50, 50, 50);
  theme_light.text_secondary = nvgRGB(100, 100, 100);
  theme_light.shadow = nvgRGBA(0, 0, 0, 30);

  theme_dark.bg = nvgRGB(14, 15, 18);
  theme_dark.display_bg = nvgRGB(14, 15, 18);
  theme_dark.btn_bg_digit = nvgRGB(30, 32, 36);
  theme_dark.btn_bg_op = nvgRGB(47, 128, 255);
  theme_dark.btn_bg_action = nvgRGB(40, 42, 46);
  theme_dark.text_primary = nvgRGB(255, 255, 255);
  theme_dark.text_secondary = nvgRGB(150, 150, 150);
  theme_dark.shadow = nvgRGBA(0, 0, 0, 128);

  current_theme = &theme_dark;
}

void initGraphButtons(int page) {
  numGraphButtons = 0;
  char *labels[48];
  int count = 0;

  if (page == 0) { // NUM
    char *numLabels[] = {
        "x",   "y", "x^2",  "x^n", "7", "8", "9", "/", "func", "(", ")", "CLR",
        "abs", ",", "<=",   ">=",  "4", "5", "6", "*", "bksp", "<", ">", "ENT",
        "ABC", " ", "sqrt", "pi",  "1", "2", "3", "-", " ",    " ", " ", " ",
        " ",   " ", " ",    " ",   "0", ".", "=", "+"};
    count = 44;
    for (int i = 0; i < count; i++)
      labels[i] = numLabels[i];
  } else if (page == 1) { // ABC
    char *abcLabels[] = {"q", "w",    "e",   "r", "t", "y",   "u",   "i", "o",
                         "p", "bksp", "CLR", "a", "s", "d",   "f",   "g", "h",
                         "j", "k",    "l",   " ", " ", "ENT", "z",   "x", "c",
                         "v", "b",    "n",   "m", ",", ".",   "123", " ", " "};
    count = 36;
    for (int i = 0; i < count; i++) {
      if (i < 34)
        labels[i] = abcLabels[i];
      else
        labels[i] = " ";
    }
  } else if (page == 2) { // FUNC
    char *funcLabels[] = {
        "sin", "cos", "tan",  "log",  "ln",   "abs",  "sign", "floor", "ceil",
        "(",   ")",   "CLR",  "asin", "acos", "atan", "sinh", "cosh",  "tanh",
        "mod", "^",   "sqrt", "sqrt", "pi",   "bksp", "123",  " ",     " ",
        " ",   " ",   " ",    " ",    " ",    " ",    " ",    " ",     "ENT"};
    count = 36;
    for (int i = 0; i < count; i++)
      labels[i] = funcLabels[i];
  }

  for (int i = 0; i < count; i++) {
    Button *b = &graphButtons[numGraphButtons++];
    strncpy(b->label, labels[i], sizeof(b->label) - 1);
    b->label[sizeof(b->label) - 1] = '\0';

    // Determine role and color
    b->role = 2; // Default action
    if (isdigit(b->label[0]) || (strlen(b->label) == 1 && b->label[0] == '.'))
      b->role = 0;
    if (strlen(b->label) == 1 && strchr("+-*/=", b->label[0]))
      b->role = 1;

    b->color = (b->role == 0)   ? current_theme->btn_bg_digit
               : (b->role == 1) ? current_theme->btn_bg_op
                                : current_theme->btn_bg_action;
    b->is_hovered = 0;
    b->anim_t = 0;
  }
}

void initButtons(void) {
  initTheme();
  numButtons = 0;

  int nums[3][3] = {{7, 8, 9}, {4, 5, 6}, {1, 2, 3}};
  for (int row = 0; row < 3; row++) {
    for (int col = 0; col < 3; col++) {
      Button *b = &buttons[numButtons++];
      snprintf(b->label, sizeof(b->label), "%d", nums[row][col]);
      b->role = 0;
      b->color = current_theme->btn_bg_digit;
    }
  }

  Button *b0 = &buttons[numButtons++];
  strcpy(b0->label, "0");
  b0->role = 0;
  b0->color = current_theme->btn_bg_digit;

  Button *bdot = &buttons[numButtons++];
  strcpy(bdot->label, ".");
  bdot->role = 0;
  bdot->color = current_theme->btn_bg_digit;

  Button *beq = &buttons[numButtons++];
  strcpy(beq->label, "=");
  beq->role = 1;
  beq->color = current_theme->btn_bg_op;

  char ops[4] = {'+', '-', '*', '/'};
  for (int i = 0; i < 4; i++) {
    Button *bop = &buttons[numButtons++];
    snprintf(bop->label, sizeof(bop->label), "%c", ops[i]);
    bop->role = 1;
    bop->color = current_theme->btn_bg_op;
  }

  strcpy(cBtn.label, "C");
  cBtn.role = 2;
  cBtn.color = current_theme->btn_bg_action;

  strcpy(histBtn.label, "H");
  histBtn.role = 2;
  histBtn.color = current_theme->btn_bg_action;

  strcpy(drawBtn.label, "DRAW");
  drawBtn.role = 2;
  drawBtn.color = current_theme->btn_bg_action;

  strcpy(modeBtn.label, "M");
  modeBtn.role = 2;
  modeBtn.color = current_theme->btn_bg_action;

  if (numButtons < 30) {
    char *sciLabels[] = {"sin",  "cos", "tan", "log", "ln",
                         "sqrt", "sqr", "x^y", "PI",  "e"};
    for (int i = 0; i < 10; i++) {
      Button *b = &buttons[numButtons++];
      strcpy(b->label, sciLabels[i]);
      b->role = 2;
      b->color = current_theme->btn_bg_action;
    }

    char *unitLabels[] = {"cm2in", "in2cm", "kg2lb", "lb2kg",
                          "km2mi", "mi2km", "C2F",   "F2C"};
    for (int i = 0; i < 8; i++) {
      Button *b = &buttons[numButtons++];
      strcpy(b->label, unitLabels[i]);
      b->role = 2;
      b->color = current_theme->btn_bg_action;
    }

    char *rpnLabels[] = {"SWP", "DRP"};
    for (int i = 0; i < 2; i++) {
      Button *b = &buttons[numButtons++];
      strcpy(b->label, rpnLabels[i]);
      b->role = 2;
      b->color = current_theme->btn_bg_action;
    }
  }

  initGraphButtons(graphKeypadPage);

  updateLayout(winWidth, winHeight);
}

void handleButtonClick(int x, int y) {
  SDL_Window *win = gWindow;
  int w, h;
  SDL_GetWindowSize(win, &w, &h);

  if (x >= histBtn.x && x < histBtn.x + histBtn.w && y >= histBtn.y &&
      y < histBtn.y + histBtn.h) {
    showHistory = !showHistory;
    if (showHistory)
      showDraw = 0;
    SDL_SetWindowSize(win, showHistory ? w + 200 : w - 200, h);
    updateLayout(showHistory ? w + 200 : w - 200, h);
    triggerClickAnim(2, 0);
    return;
  }

  if (x >= modeBtn.x && x < modeBtn.x + modeBtn.w && y >= modeBtn.y &&
      y < modeBtn.y + modeBtn.h) {
    isDropdownOpen = !isDropdownOpen;
    return;
  }

  if (isDropdownOpen) {

    float rx = modeBtn.x;
    float ry = modeBtn.y;
    float rh = modeBtn.h;
    int itemH = 30;

    if (x >= rx && x < rx + 100 && y >= ry + rh && y < ry + rh + itemH) {
      currentMode = MODE_BASIC;
      isDropdownOpen = 0;
      showDraw = 0;
      SDL_SetWindowSize(win, 300, h);
      updateLayout(300, h);
      return;
    }

    if (x >= rx && x < rx + 100 && y >= ry + rh + itemH &&
        y < ry + rh + 2 * itemH) {
      currentMode = MODE_SCIENTIFIC;
      isDropdownOpen = 0;
      showDraw = 0;
      SDL_SetWindowSize(win, 450, h);
      updateLayout(450, h);
      return;
    }

    if (x >= rx && x < rx + 100 && y >= ry + rh + 2 * itemH &&
        y < ry + rh + 3 * itemH) {
      currentMode = MODE_UNIT;
      isDropdownOpen = 0;
      showDraw = 0;
      SDL_SetWindowSize(win, 450, h);
      updateLayout(450, h);
      return;
    }

    if (x >= rx && x < rx + 100 && y >= ry + rh + 3 * itemH &&
        y < ry + rh + 4 * itemH) {
      currentMode = MODE_RPN;
      isDropdownOpen = 0;
      showDraw = 0;
      SDL_SetWindowSize(win, 650, h);
      updateLayout(650, h);
      return;
    }

    if (x >= rx && x < rx + 100 && y >= ry + rh + 4 * itemH &&
        y < ry + rh + 5 * itemH) {
      currentMode = MODE_DRAW;
      showDraw = 1;
      showHistory = 0;
      isDropdownOpen = 0;
      SDL_SetWindowSize(win, 300, h);
      updateLayout(300, h);
      return;
    }

    if (x >= rx && x < rx + 100 && y >= ry + rh + 5 * itemH &&
        y < ry + rh + 6 * itemH) {
      currentMode = MODE_GRAPH;
      showDraw = 0;
      showHistory = 0;
      isDropdownOpen = 0;
      graphKeypadPage = 0;
      initGraphButtons(0);
      SDL_SetWindowSize(win, 1000, 500);
      updateLayout(1000, 500);
      return;
    }

    isDropdownOpen = 0;
    return;
  }

  if (currentMode == MODE_GRAPH) {
    // Handle Sidebar toggle
    if (x > sidebarX + sidebarW - 40 && x < sidebarX + sidebarW && y < 50) {
      isSidebarExpanded = !isSidebarExpanded;
      updateLayout(w, h);
      return;
    }

    // Handle graph buttons
    for (int i = 0; i < numGraphButtons; i++) {
      Button *b = &graphButtons[i];
      if (x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h) {
        if (strcmp(b->label, "CLR") == 0) {
          graphEq[0] = '\0';
        } else if (strcmp(b->label, "bksp") == 0) {
          int len = strlen(graphEq);
          if (len > 0)
            graphEq[len - 1] = '\0';
        } else if (strcmp(b->label, "ABC") == 0) {
          graphKeypadPage = 1;
          initGraphButtons(1);
          updateLayout(w, h);
        } else if (strcmp(b->label, "123") == 0) {
          graphKeypadPage = 0;
          initGraphButtons(0);
          updateLayout(w, h);
        } else if (strcmp(b->label, "func") == 0) {
          graphKeypadPage = 2;
          initGraphButtons(2);
          updateLayout(w, h);
        } else if (strcmp(b->label, "ENT") == 0) {
          // Re-render handled by main loop
        } else if (strlen(b->label) > 0 && strcmp(b->label, " ") != 0) {
          if (strcmp(b->label, "x^2") == 0) {
            strncat(graphEq, "^2", sizeof(graphEq) - strlen(graphEq) - 1);
          } else if (strcmp(b->label, "x^n") == 0) {
            strncat(graphEq, "^", sizeof(graphEq) - strlen(graphEq) - 1);
          } else if (strcmp(b->label, "sqrt") == 0) {
            strncat(graphEq, "sqrt(", sizeof(graphEq) - strlen(graphEq) - 1);
          } else if (strcmp(b->label, "abs") == 0) {
            strncat(graphEq, "abs(", sizeof(graphEq) - strlen(graphEq) - 1);
          } else if (strcmp(b->label, "pi") == 0) {
            strncat(graphEq, "pi", sizeof(graphEq) - strlen(graphEq) - 1);
          } else {
            char *pats[] = {"sin",  "cos",   "tan",  "log",  "ln",
                            "sign", "floor", "ceil", "asin", "acos",
                            "atan", "sinh",  "cosh", "tanh", "mod"};
            int isFunc = 0;
            for (int k = 0; k < 15; k++) {
              if (strcmp(b->label, pats[k]) == 0) {
                strncat(graphEq, b->label,
                        sizeof(graphEq) - strlen(graphEq) - 1);
                strncat(graphEq, "(", sizeof(graphEq) - strlen(graphEq) - 1);
                isFunc = 1;
                break;
              }
            }
            if (!isFunc) {
              strncat(graphEq, b->label, sizeof(graphEq) - strlen(graphEq) - 1);
            }
          }
        }
        triggerClickAnim(5, i);
        return;
      }
    }
    return;
  }

  if (x >= cBtn.x && x < cBtn.x + cBtn.w && y >= cBtn.y &&
      y < cBtn.y + cBtn.h) {
    recordInput("C");
    calc_inputClear();
    specialMessage[0] = '\0';
    triggerClickAnim(1, 0);
    return;
  }

  if (showDraw || currentMode == MODE_DRAW) {
    int calcWidth = showHistory ? w - 200 : w;
    int gridStartY = 120;

    int reservedBottom = 50;
    int padH = h - gridStartY - 20 - reservedBottom;
    int padW = calcWidth - 40;
    if (padH < 100)
      padH = 100;

    int cellSize = (padW < padH ? padW : padH) / 28;
    int gridSize = 28 * cellSize;
    int gridX = 20 + (padW - gridSize) / 2;
    int gridY = gridStartY + (padH - gridSize) / 2;

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

          calc_inputOperator(drLabels[i][0]);
          return;
        }
      }
    }

    if (x >= gridX && x < gridX + gridSize && y >= gridY &&
        y < gridY + gridSize) {
      int col = (x - gridX) / cellSize;
      int row = (y - gridY) / cellSize;
      if (col >= 0 && col < 28 && row >= 0 && row < 28) {
        drawGrid[row][col] = 1;
        isDrawing = 1;
        lastDrawTime = SDL_GetTicks();
        hasDrawnSomething = 1;
      }
      return;
    }
    return;
  }

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

  for (int i = 0; i < numButtons; i++) {
    Button *b = &buttons[i];

    if (b->w > 0 && b->h > 0 && x >= b->x && x < b->x + b->w && y >= b->y &&
        y < b->y + b->h) {
      char *label = b->label;
      if ((label[0] >= '0' && label[0] <= '9')) {
        recordInput(label);
        calc_inputDigit(label);
      } else if (label[0] == '+' || label[0] == '-' || label[0] == '*' ||
                 label[0] == '/' || label[0] == '^') {
        recordInput(label);
        calc_inputOperator(label[0]);
      } else if (strcmp(label, "=") == 0) {
        recordInput("=");
        isEqualsDown = 1;
        equalsPressTime = SDL_GetTicks();
        if (currentMode == MODE_RPN) {
          calc_inputRPN("ENT");
        } else {
          calc_inputEquals();
        }
      } else if (strcmp(label, ".") == 0) {
        recordInput(".");
        calc_inputDigit(".");
      } else if (strcmp(label, "ENT") == 0) {
        recordInput(label);
        calc_inputRPN("ENT");
      } else if (strcmp(label, "sin") == 0 || strcmp(label, "cos") == 0 ||
                 strcmp(label, "tan") == 0 || strcmp(label, "log") == 0 ||
                 strcmp(label, "ln") == 0 || strcmp(label, "sqr") == 0 ||
                 strcmp(label, "sqrt") == 0) {
        recordInput(label);
        calc_inputUnary(label);
      } else if (strcmp(label, "x^y") == 0) {
        recordInput(label);
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

  int konamiPattern[KONAMI_LENGTH] = {
      SDLK_UP,    SDLK_UP,   SDLK_DOWN,  SDLK_DOWN, SDLK_LEFT,
      SDLK_RIGHT, SDLK_LEFT, SDLK_RIGHT, SDLK_b,    SDLK_a};

  if (konamiIndex < KONAMI_LENGTH && key == konamiPattern[konamiIndex]) {
    konamiSequence[konamiIndex] = key;
    konamiIndex++;
    if (konamiIndex == KONAMI_LENGTH) {
      isRainbowMode = !isRainbowMode;
      konamiIndex = 0;
      return;
    }
  } else if (key == konamiPattern[0]) {
    konamiIndex = 1;
    konamiSequence[0] = key;
  } else if (key != SDLK_UP && key != SDLK_DOWN && key != SDLK_LEFT &&
             key != SDLK_RIGHT && key != SDLK_b && key != SDLK_a) {

    konamiIndex = 0;
  }

  if (currentMode == MODE_GRAPH) {
    if (key == SDLK_BACKSPACE) {
      int len = strlen(graphEq);
      if (len > 0)
        graphEq[len - 1] = '\0';
      return;
    }
    if (key == SDLK_DELETE) {
      graphEq[0] = '\0';
      return;
    }
    if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_ESCAPE) {
      return;
    }
    if (key >= 32 && key <= 126) {
      char s[2] = {(char)key, '\0'};
      strncat(graphEq, s, sizeof(graphEq) - strlen(graphEq) - 1);
      return;
    }
    return;
  }

  if (currentMode == MODE_DRAW) {

    return;
  }

  if (key >= SDLK_0 && key <= SDLK_9) {
    char digit[2] = {(char)key, '\0'};
    calc_inputDigit(digit);
    return;
  }

  if ((key >= SDLK_KP_1 && key <= SDLK_KP_9) || key == SDLK_KP_0) {
    char digit[2] = {'\0', '\0'};
    if (key == SDLK_KP_0)
      digit[0] = '0';
    else
      digit[0] = (char)('1' + (key - SDLK_KP_1));
    calc_inputDigit(digit);
    return;
  }

  if (key == SDLK_PERIOD || key == SDLK_KP_PERIOD) {
    calc_inputDigit(".");
    return;
  }

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
  case SDLK_CARET:
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
    recordInput("C");
    calc_inputClear();
    specialMessage[0] = '\0';
    break;
  case SDLK_BACKSPACE:
    calc_inputBackspace();
    break;
  }
}
void ui_handle_mouse_move(int x, int y) {
  for (int i = 0; i < numButtons; i++) {
    Button *b = &buttons[i];
    if (x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h) {
      b->is_hovered = 1;
    } else {
      b->is_hovered = 0;
    }
  }

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
void ui_init_nanovg(void) {

  vg = nvgCreateGL3(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
  if (vg == NULL) {
    printf("Could not init nanovg.\n");
    exit(1);
  }

#ifdef __APPLE__
  fontNormal = nvgCreateFont(vg, "sans", "/System/Library/Fonts/Helvetica.ttc");
  fontBold =
      nvgCreateFont(vg, "sans-bold", "/System/Library/Fonts/Helvetica.ttc");
#else

  fontNormal = nvgCreateFont(vg, "sans",
                             "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
  if (fontNormal == -1) {

    fontNormal = nvgCreateFont(
        vg, "sans", "/usr/share/fonts/truetype/freefont/FreeSans.ttf");
  }

  fontBold = fontNormal;
#endif

  if (fontNormal == -1) {
    printf("Could not find font. Text will be missing.\n");
  }
}
void draw_rrect_shadow(NVGcontext *vg, float x, float y, float w, float h,
                       float rad, NVGcolor bg, NVGcolor shadow) {

  NVGpaint shadowPaint =
      nvgBoxGradient(vg, x, y + 2, w, h, rad, 10, shadow, nvgRGBA(0, 0, 0, 0));
  nvgBeginPath(vg);
  nvgRect(vg, x - 10, y - 10, w + 20, h + 20);
  nvgRoundedRect(vg, x, y, w, h, rad);
  nvgPathWinding(vg, NVG_HOLE);
  nvgFillPaint(vg, shadowPaint);
  nvgFill(vg);

  nvgBeginPath(vg);
  nvgRoundedRect(vg, x, y, w, h, rad);
  nvgFillColor(vg, bg);
  nvgFill(vg);
  nvgFill(vg);
}

void draw_crash_screen(NVGcontext *vg, int w, int h) {

  nvgBeginPath(vg);
  nvgRect(vg, 0, 0, w, h);
  nvgFillColor(vg, nvgRGB(0, 0, 150));
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
void draw_404_screen(NVGcontext *vg, int w, int h) {

  nvgBeginPath(vg);
  nvgRect(vg, 0, 0, w, h);
  nvgFillColor(vg, nvgRGB(0, 0, 0));
  nvgFill(vg);

  nvgFillColor(vg, nvgRGB(255, 0, 0));
  nvgFillColor(vg, nvgRGB(255, 255, 255));

  nvgFontSize(vg, 80);
  nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
  nvgText(vg, w / 2, h / 2 - 20, "404", NULL);

  nvgFontSize(vg, 30);
  nvgText(vg, w / 2, h / 2 + 30, "NOT FOUND", NULL);
}
void draw_graph_grid(NVGcontext *vg, float x, float y, float w, float h) {
  nvgSave(vg);
  nvgScissor(vg, x, y, w, h);

  // Background
  nvgBeginPath(vg);
  nvgRect(vg, x, y, w, h);
  nvgFillColor(vg, current_theme->bg);
  nvgFill(vg);

  float scaleX = w / (xMax - xMin);
  float scaleY = h / (yMax - yMin);

  // Grid lines
  nvgStrokeWidth(vg, 1.0f);
  nvgStrokeColor(vg, nvgRGBA(128, 128, 128, 50));
  for (float i = ceil(xMin); i <= floor(xMax); i += 1.0f) {
    float px = x + (i - xMin) * scaleX;
    nvgBeginPath(vg);
    nvgMoveTo(vg, px, y);
    nvgLineTo(vg, px, y + h);
    nvgStroke(vg);
  }
  for (float j = ceil(yMin); j <= floor(yMax); j += 1.0f) {
    float py = y + h - (j - yMin) * scaleY;
    nvgBeginPath(vg);
    nvgMoveTo(vg, x, py);
    nvgLineTo(vg, x + w, py);
    nvgStroke(vg);
  }

  // Axes
  nvgStrokeWidth(vg, 2.0f);
  nvgStrokeColor(vg, current_theme->text_primary);
  float zeroX = x + (0 - xMin) * scaleX;
  float zeroY = y + h - (0 - yMin) * scaleY;

  if (zeroX >= x && zeroX <= x + w) {
    nvgBeginPath(vg);
    nvgMoveTo(vg, zeroX, y);
    nvgLineTo(vg, zeroX, y + h);
    nvgStroke(vg);
  }
  if (zeroY >= y && zeroY <= y + h) {
    nvgBeginPath(vg);
    nvgMoveTo(vg, x, zeroY);
    nvgLineTo(vg, x + w, zeroY);
    nvgStroke(vg);
  }

  nvgRestore(vg);
}

void draw_graph_curve(NVGcontext *vg, float x, float y, float w, float h) {
  if (strlen(graphEq) == 0)
    return;

  nvgSave(vg);
  nvgScissor(vg, x, y, w, h);

  int inequality = 0; // 0: none, 1: <, 2: >, 3: <=, 4: >=
  if (strstr(graphEq, "<="))
    inequality = 3;
  else if (strstr(graphEq, ">="))
    inequality = 4;
  else if (strchr(graphEq, '<'))
    inequality = 1;
  else if (strchr(graphEq, '>'))
    inequality = 2;

  float scaleY = h / (yMax - yMin);

  if (inequality > 0) {
    nvgBeginPath(vg);
    nvgFillColor(vg, nvgRGBA(47, 128, 255, 64)); // Light blue shade

    for (int i = 0; i <= w; i++) {
      float xv = xMin + (float)i / (float)w * (xMax - xMin);
      float yv = (float)evaluate_graph(graphEq, xv);

      if (isnan(yv) || isinf(yv) || yv > 1e6 || yv < -1e6)
        continue;

      float px = x + i;
      float py = y + h - (yv - yMin) * scaleY;

      if (i == 0) {
        if (inequality == 1 || inequality == 3)
          nvgMoveTo(vg, px, y + h); // Fill from bottom
        else
          nvgMoveTo(vg, px, y); // Fill from top
      }
      nvgLineTo(vg, px, py);
    }

    // Close the path
    if (inequality == 1 || inequality == 3) {
      nvgLineTo(vg, x + w, y + h);
      nvgLineTo(vg, x, y + h);
    } else {
      nvgLineTo(vg, x + w, y);
      nvgLineTo(vg, x, y);
    }
    nvgFill(vg);
  }

  // Draw the boundary line
  nvgBeginPath(vg);
  nvgStrokeWidth(vg, 2.0f);
  nvgStrokeColor(vg, nvgRGB(47, 128, 255));

  int first = 1;
  for (int i = 0; i <= w; i++) {
    float xv = xMin + (float)i / (float)w * (xMax - xMin);
    float yv = (float)evaluate_graph(graphEq, xv);

    if (isnan(yv) || isinf(yv) || yv > 1e6 || yv < -1e6) {
      if (!first) {
        nvgStroke(vg);
        nvgBeginPath(vg);
        first = 1;
      }
      continue;
    }

    float px = x + i;
    float py = y + h - (yv - yMin) * scaleY;

    if (first) {
      nvgMoveTo(vg, px, py);
      first = 0;
    } else {
      nvgLineTo(vg, px, py);
    }
  }
  nvgStroke(vg);
  nvgRestore(vg);
}

void draw_button_render(NVGcontext *vg, Button *b, float dt) {
  if (b->w <= 0)
    return;

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

  if (isRainbowMode) {
    c = hsvToRgb(rainbowHue, 0.7f, 0.8f);
  }

  if (b->anim_t > 0) {
    c.r += b->anim_t * 0.1f;
    c.g += b->anim_t * 0.1f;
    c.b += b->anim_t * 0.1f;
  }

  draw_rrect_shadow(vg, b->x, b->y, b->w, b->h, 14, c, current_theme->shadow);

  nvgFillColor(vg, current_theme->text_primary);
  if (b->role == 1 && current_theme == &theme_dark)
    nvgFillColor(vg, nvgRGB(255, 255, 255));

  float fsize = b->h * 0.5f;
  if (fsize < 18)
    fsize = 18;
  if (fsize > 40)
    fsize = 40;

  nvgFontSize(vg, fsize);
  nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
  nvgText(vg, b->x + b->w / 2, b->y + b->h / 2, b->label, NULL);
}

void draw_graph_sidebar(NVGcontext *vg, float x, float y, float w, float h) {
  if (w <= 0)
    return;

  nvgBeginPath(vg);
  nvgRect(vg, x, y, w, h);
  nvgFillColor(vg, current_theme->btn_bg_action);
  nvgFill(vg);

  // Expression input box
  float boxX = x + 10;
  float boxY = y + 50;
  float boxW = w - 20;
  float boxH = 40;

  nvgBeginPath(vg);
  nvgRoundedRect(vg, boxX, boxY, boxW, boxH, 5);
  nvgFillColor(vg, current_theme->display_bg);
  nvgFill(vg);
  nvgStrokeColor(vg, nvgRGB(47, 128, 255));
  nvgStroke(vg);

  nvgFillColor(vg, current_theme->text_primary);
  nvgFontSize(vg, 18);
  nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
  nvgText(vg, boxX + 10, boxY + boxH / 2, graphEq, NULL);

  // Sidebar Title
  nvgFontSize(vg, 18);
  nvgText(vg, x + 10, y + 25, "Expressions", NULL);

  // Toggle Icon
  nvgFontSize(vg, 18);
  nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
  nvgText(vg, x + w - 10, y + 25, isSidebarExpanded ? "<<" : ">>", NULL);
}

void draw_graph_keypad(NVGcontext *vg, float x, float y, float w, float h) {
  for (int i = 0; i < numGraphButtons; i++) {
    draw_button_render(vg, &graphButtons[i], 0);
  }
}

void ui_render(SDL_Window *win) {
  int w, h;
  SDL_GetWindowSize(win, &w, &h);
  int winWidth, winHeight;
  SDL_GL_GetDrawableSize(win, &winWidth, &winHeight);
  float pxRatio = (float)winWidth / (float)w;
  float dt = 0.016f;

  static int lastW = 0, lastH = 0;
  if (w != lastW || h != lastH) {
    updateLayout(w, h);
    lastW = w;
    lastH = h;
  }

  nvgBeginFrame(vg, w, h, pxRatio);

  if (is404Mode) {
    draw_404_screen(vg, w, h);
    nvgEndFrame(vg);
    return;
  }

  nvgBeginPath(vg);
  nvgRect(vg, 0, 0, w, h);
  nvgFillColor(vg, current_theme->bg);
  nvgFill(vg);

  if (currentMode == MODE_GRAPH) {
    // Update graph buttons hover & anim
    int mouseX, mouseY;
    SDL_GetMouseState(&mouseX, &mouseY);
    for (int i = 0; i < numGraphButtons; i++) {
      Button *b = &graphButtons[i];
      if (mouseX >= b->x && mouseX < b->x + b->w && mouseY >= b->y &&
          mouseY < b->y + b->h) {
        b->is_hovered = 1;
        b->anim_t += dt * 5.0f;
        if (b->anim_t > 1.0f)
          b->anim_t = 1.0f;
      } else {
        b->is_hovered = 0;
        b->anim_t -= dt * 5.0f;
        if (b->anim_t < 0.0f)
          b->anim_t = 0.0f;
      }
    }

    draw_graph_grid(vg, graphAreaX, graphAreaY, graphAreaW, graphAreaH);
    draw_graph_curve(vg, graphAreaX, graphAreaY, graphAreaW, graphAreaH);
    draw_graph_sidebar(vg, sidebarX, sidebarY, sidebarW, sidebarH);
    draw_graph_keypad(vg, keypadX, keypadY, keypadW, keypadH);

    draw_button_render(vg, &modeBtn, dt);
  } else {
    if (showHistory || currentMode == MODE_RPN) {
      nvgBeginPath(vg);
      nvgRect(vg, w - 200, 0, 200, h);
      nvgFillColor(vg, nvgRGB(40, 40, 40));
      nvgFill(vg);

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

    nvgBeginPath(vg);
    nvgRoundedRect(vg, displayX, displayY, displayW, displayH, 10);
    nvgFillColor(vg, current_theme->display_bg);
    nvgFill(vg);

    char formattedText[64];
    if (strlen(specialMessage) > 0) {
      snprintf(formattedText, sizeof(formattedText), "%s", specialMessage);
    } else if (isEqualsDown && SDL_GetTicks() - equalsPressTime > 2000) {
      snprintf(formattedText, sizeof(formattedText), "why are you holding me");
    } else {
      formatNumber(calc.display, formattedText, sizeof(formattedText));
      double val = atof(calc.display);
      if (fabs(val - 80085) < 1e-9)
        snprintf(formattedText, sizeof(formattedText), "BOOBS");
      else if (fabs(val - 69) < 1e-9)
        snprintf(formattedText, sizeof(formattedText), "NICE");
      else if (fabs(val - 420) < 1e-9)
        snprintf(formattedText, sizeof(formattedText), "BLAZE IT");
      else if (fabs(val - 1337) < 1e-9)
        snprintf(formattedText, sizeof(formattedText), "LEET");
    }

    nvgFillColor(vg, current_theme->text_primary);
    float dispFont = h * 0.08f;
    if (dispFont < 36)
      dispFont = 36;
    if (dispFont > 80)
      dispFont = 80;
    nvgFontSize(vg, dispFont);

    float bounds[4];
    nvgTextBounds(vg, 0, 0, formattedText, NULL, bounds);
    float textW = bounds[2] - bounds[0];
    float maxW = displayW - 20;
    if (textW > maxW) {
      dispFont *= (maxW / textW);
      if (dispFont < 12)
        dispFont = 12;
      nvgFontSize(vg, dispFont);
    }

    nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    nvgText(vg, displayX + displayW - 10, displayY + 25, formattedText, NULL);

    if (isPrimeResult) {
      nvgBeginPath(vg);
      nvgFillColor(vg, nvgRGB(255, 215, 0));
      nvgFontSize(vg, 12);
      nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
      nvgText(vg, displayX + 10, displayY + 5, "PRIME", NULL);
    }
    for (int i = 0; i < numButtons; i++) {
      if (strcmp(buttons[i].label, "=") == 0 ||
          strcmp(buttons[i].label, "ENT") == 0) {
        if (currentMode == MODE_RPN)
          strcpy(buttons[i].label, "ENT");
        else
          strcpy(buttons[i].label, "=");
      }
    }

    draw_button_render(vg, &modeBtn, dt);
    draw_button_render(vg, &histBtn, dt);
    draw_button_render(vg, &cBtn, dt);

    for (int i = 0; i < numButtons; i++) {
      draw_button_render(vg, &buttons[i], dt);
    }
  }

  if (isDropdownOpen) {
    float rx = modeBtn.x;
    float ry = modeBtn.y + modeBtn.h + 5;
    float Rw = 120, Rh = 180;

    draw_rrect_shadow(vg, rx, ry, Rw, Rh, 5, nvgRGB(50, 50, 50),
                      nvgRGBA(0, 0, 0, 100));

    nvgFillColor(vg, nvgRGB(255, 255, 255));
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgText(vg, rx + 10, ry + 20, "Basic", NULL);
    nvgText(vg, rx + 10, ry + 50, "Scientific", NULL);
    nvgText(vg, rx + 10, ry + 80, "Unit", NULL);
    nvgText(vg, rx + 10, ry + 110, "RPN", NULL);
    nvgText(vg, rx + 10, ry + 140, "Draw", NULL);
    nvgText(vg, rx + 10, ry + 170, "Graphing", NULL);
  }

  if (showDraw || currentMode == MODE_DRAW) {

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

      b.x = startBtnX + i * (btnW + gap);
      b.y = btnY;
      b.w = btnW;
      b.h = btnH;
      strcpy(b.label, drLabels[i]);
      b.role = (strcmp(drLabels[i], "CLR") == 0) ? 2 : 1;
      b.is_hovered = 0;

      int mouseX, mouseY;
      SDL_GetMouseState(&mouseX, &mouseY);
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

  if (isCrashMode) {
    draw_crash_screen(vg, w, h);
  }

  if (isDevMode) {

    frameCount++;
    Uint32 currentTime = SDL_GetTicks();
    if (currentTime - lastFPSUpdate > 1000) {
      fps = frameCount * 1000.0f / (currentTime - lastFPSUpdate);
      frameCount = 0;
      lastFPSUpdate = currentTime;
    }

    nvgFontSize(vg, 14);
    nvgFillColor(vg, nvgRGB(0, 255, 0));
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);

    char debugText[256];
    snprintf(debugText, sizeof(debugText), "FPS: %.1f", fps);
    nvgText(vg, w - 100, 5, debugText, NULL);

    snprintf(debugText, sizeof(debugText), "Input: %s", inputSequence);
    nvgText(vg, 10, h - 60, debugText, NULL);

    snprintf(debugText, sizeof(debugText), "Display: %s", calc.display);
    nvgText(vg, 10, h - 45, debugText, NULL);

    snprintf(debugText, sizeof(debugText), "Stored: %.5g | Op: %c",
             calc.storedValue, calc.pendingOp ? calc.pendingOp : ' ');
    nvgText(vg, 10, h - 30, debugText, NULL);

    snprintf(debugText, sizeof(debugText), "Pending: %d | Mode: %d",
             calc.hasPendingOp, currentMode);
    nvgText(vg, 10, h - 15, debugText, NULL);
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

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

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

  set_macos_window_style(win);

  SDL_GL_SetSwapInterval(1);

  ui_init_nanovg();

  if (model_load("model.bin", &nn)) {
    printf("Successfully loaded model.bin\n");
    modelLoaded = 1;
  } else {
  }

  load_state();

  time_t now = time(NULL);
  struct tm *local = localtime(&now);
  int hour = local->tm_hour;

  if (hour == 0) {
    strcpy(calc.display, "go sleep bro");
  } else if (hour == 3) {
    strcpy(calc.display, "insomnia mode activated");
  } else if (hour == 12) {
    strcpy(calc.display, "lunch break?");
  }

  initButtons();

  int quit = 0;
  SDL_Event e;

  while (!quit) {
    int mouseX, mouseY;
    SDL_GetMouseState(&mouseX, &mouseY);
    ui_handle_mouse_move(mouseX, mouseY);

    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) {
        quit = 1;
      } else if (e.type == SDL_MOUSEBUTTONDOWN) {
        handleButtonClick(e.button.x, e.button.y);
      } else if (e.type == SDL_MOUSEBUTTONUP) {
        isDrawing = 0;
        isEqualsDown = 0;
      } else if (e.type == SDL_MOUSEMOTION && isDrawing && showDraw) {

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
            drawGrid[row][col] = 1;
            lastDrawTime = SDL_GetTicks();
            hasDrawnSomething = 1;
          }
        }
      } else if (e.type == SDL_KEYDOWN) {
        handleKeyboard(e.key.keysym.sym);
      }
    }

    if (showDraw && hasDrawnSomething && !isDrawing) {
      Uint32 now = SDL_GetTicks();
      if (now - lastDrawTime > AUTO_PREDICT_DELAY) {
        predictedDigit = predictDigit();

        if (predictedDigit == -2) {

          isHeartAnimActive = 1;
          easterEggStart = SDL_GetTicks();
        } else if (predictedDigit >= 0 && predictedDigit <= 9) {

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

        for (int r = 0; r < 28; r++) {
          for (int c = 0; c < 28; c++) {
            drawGrid[r][c] = 0;
          }
        }
        hasDrawnSomething = 0;
      }
    }

    if (isRainbowMode) {
      rainbowHue += 2.0f;
      if (rainbowHue >= 360.0f)
        rainbowHue -= 360.0f;
    }

    ui_render(win);
  }

  save_state();

  SDL_GL_DeleteContext(glContext);
  SDL_DestroyWindow(win);
  SDL_Quit();
  return 0;
}
void save_state() {
  FILE *f = fopen("calc_state.dat", "wb");
  if (!f)
    return;

  fwrite(&calc, sizeof(Calculator), 1, f);

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
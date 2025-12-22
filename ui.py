import pyautogui
import time
import random

pyautogui.FAILSAFE = True

START_DELAY = 5

MIN_KEY_DELAY = 0.02
MAX_KEY_DELAY = 0.11

THINK_MIN = 0.3
THINK_MAX = 1.5

types = ["int", "float", "double", "char"]
ops = ["+", "-", "*", "/"]
conditions = ["<", ">", "<=", ">=", "==", "!="]

# ------------------ HUMAN TYPING ------------------

def human_type(text):
    for ch in text:
        pyautogui.write(ch)
        time.sleep(random.uniform(MIN_KEY_DELAY, MAX_KEY_DELAY))

# ------------------ RANDOM BUILDERS ------------------

def rand_var():
    name = f"var_{random.randint(1,999)}"
    t = random.choice(types)
    val = random.randint(0, 100)
    return f"{t} {name} = {val};\n"

def rand_if():
    a = random.randint(1,50)
    b = random.randint(1,50)
    cond = random.choice(conditions)
    return f"""
if ({a} {cond} {b}) {{
    // condition true
}} else {{
    // condition false
}}
"""

def rand_for():
    i = random.choice(["i","j","k"])
    limit = random.randint(5,50)
    return f"""
for (int {i} = 0; {i} < {limit}; {i}++) {{
    // loop body
}}
"""

def rand_while():
    limit = random.randint(5,30)
    return f"""
int counter = 0;
while (counter < {limit}) {{
    counter++;
}}
"""

def rand_struct():
    name = f"Struct_{random.randint(1,999)}"
    field = random.choice(types)
    return f"""
typedef struct {name} {{
    {field} value;
}} {name};
"""

def rand_function():
    name = f"func_{random.randint(1,999)}"
    body_parts = random.sample(
        [rand_var, rand_if, rand_for, rand_while],
        random.randint(1,3)
    )

    body = ""
    for part in body_parts:
        body += part()

    return f"""
void {name}() {{
{body}
}}
"""

# ------------------ MAIN LOOP ------------------

def main():
    print(f"Starting in {START_DELAY} seconds...")
    time.sleep(START_DELAY)

    generators = [
        rand_var,
        rand_if,
        rand_for,
        rand_while,
        rand_struct,
        rand_function
    ]

    while True:
        block = random.choice(generators)()
        human_type(block)

        # Human thinking pause
        time.sleep(random.uniform(THINK_MIN, THINK_MAX))

if __name__ == "__main__":
    main()

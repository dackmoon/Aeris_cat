# 1.4 C++ 语言基础（ESP32 固件开发）

> 最后更新：2026-04-11  
> 适用版本：Cyber Cat Phase 4/5

---

## 本章目标

完成本章后，你将：
- [ ] 掌握 C++ 基础语法和变量类型
- [ ] 理解类与对象的概念
- [ ] 了解内存管理和指针基础
- [ ] 能够使用 Arduino 框架编写固件

---

## 1.4.1 C++ 基础语法

### 程序结构

一个 C++ 程序的基本结构：

```cpp
// 1. 预处理指令（头文件包含）
#include <Arduino.h>
#include <stdio.h>

// 2. 全局变量
int globalCounter = 0;

// 3. 函数声明
void setup();
void loop();

// 4. 函数定义
void setup() {
    // 初始化代码，只执行一次
    Serial.begin(115200);
}

void loop() {
    // 主循环，不断重复
    globalCounter++;
    delay(1000);
}
```

### 变量与数据类型

**基本数据类型：**

| 类型 | 大小 | 范围 | 说明 |
|------|------|------|------|
| `bool` | 1 字节 | `true`/`false` | 布尔值 |
| `char` | 1 字节 | -128 ~ 127 | 字符 |
| `int` | 4 字节 | -21 亿 ~ 21 亿 | 整数（ESP32） |
| `float` | 4 字节 | ±3.4×10³⁸ | 浮点数 |
| `double` | 8 字节 | ±1.7×10³⁰⁸ | 双精度浮点 |
| `uint8_t` | 1 字节 | 0 ~ 255 | 无符号 8 位整数 |
| `uint32_t` | 4 字节 | 0 ~ 42 亿 | 无符号 32 位整数 |

```cpp
// 变量声明示例
bool isReady = true;
char initial = 'A';
int age = 25;
float temperature = 36.5;
uint32_t counter = 0;

// 常量声明
const int MAX_COUNT = 100;      // C++ 常量
#define PI 3.14159              // C 风格宏定义
constexpr int SIZE = 10;        // C++11 编译期常量
```

### 运算符

```cpp
// 算术运算符
int a = 10 + 5;     // 加法：15
int b = 10 - 5;     // 减法：5
int c = 10 * 5;     // 乘法：50
int d = 10 / 5;     // 除法：2
int e = 10 % 3;     // 取余：1

// 自增自减
int x = 5;
x++;        // x = 6
++x;        // x = 7
x--;        // x = 6

// 比较运算符
bool eq = (10 == 5);    // false
bool ne = (10 != 5);    // true
bool gt = (10 > 5);     // true
bool lt = (10 < 5);     // false

// 逻辑运算符
bool and_result = (true && false);  // false
bool or_result = (true || false);   // true
bool not_result = !true;            // false
```

### 控制结构

**条件语句：**

```cpp
// if-else 语句
int score = 85;

if (score >= 90) {
    Serial.println("优秀");
} else if (score >= 60) {
    Serial.println("及格");
} else {
    Serial.println("不及格");
}

// switch 语句
int day = 3;
switch (day) {
    case 1:
        Serial.println("星期一");
        break;
    case 2:
        Serial.println("星期二");
        break;
    case 3:
        Serial.println("星期三");
        break;
    default:
        Serial.println("其他");
        break;
}
```

**循环语句：**

```cpp
// for 循环
for (int i = 0; i < 5; i++) {
    Serial.printf("计数：%d\n", i);
}

// while 循环
int count = 0;
while (count < 5) {
    Serial.println(count);
    count++;
}

// do-while 循环
do {
    Serial.println("至少执行一次");
} while (false);

// 循环控制
for (int i = 0; i < 10; i++) {
    if (i == 3) continue;   // 跳过本次循环
    if (i == 7) break;      // 退出循环
    Serial.println(i);
}
```

### 数组

```cpp
// 声明数组
int numbers[5] = {1, 2, 3, 4, 5};

// 访问元素
int first = numbers[0];     // 1
int third = numbers[2];     // 3

// 修改元素
numbers[0] = 10;

// 获取数组大小
int size = sizeof(numbers) / sizeof(numbers[0]);  // 5

// 遍历数组
for (int i = 0; i < size; i++) {
    Serial.println(numbers[i]);
}
```

### 函数

```cpp
// 函数声明
int add(int a, int b);

// 函数定义
int add(int a, int b) {
    return a + b;
}

// 带默认参数的函数
void greet(String name = "Guest") {
    Serial.printf("Hello, %s!\n", name.c_str());
}

// 函数调用
int result = add(5, 3);     // result = 8
greet();                    // Hello, Guest!
greet("Alice");             // Hello, Alice!
```

---

## 1.4.2 类与对象

### 类的定义

```cpp
// 类声明
class CatFace {
private:
    // 私有成员：只能在类内部访问
    int _eyeSize;
    bool _isBlinking;
    
public:
    // 公有成员：可以在外部访问
    
    // 构造函数：创建对象时调用
    CatFace() {
        _eyeSize = 95;
        _isBlinking = false;
    }
    
    // 成员函数
    void setEyeSize(int size) {
        if (size > 0 && size <= 100) {
            _eyeSize = size;
        }
    }
    
    int getEyeSize() {
        return _eyeSize;
    }
    
    void blink() {
        _isBlinking = true;
        // 执行眨眼动画
        delay(100);
        _isBlinking = false;
    }
};
```

### 对象的创建和使用

```cpp
// 创建对象
CatFace myCat;

// 或者使用指针
CatFace* pCat = new CatFace();

// 调用成员函数
myCat.setEyeSize(80);
Serial.println(myCat.getEyeSize());   // 输出：80

myCat.blink();

// 使用指针时
pCat->blink();

// 删除对象（释放内存）
delete pCat;
```

### 继承

```cpp
// 基类
class Animal {
protected:
    String name;
    
public:
    Animal(String n) : name(n) {}
    
    void eat() {
        Serial.printf("%s 在吃东西\n", name.c_str());
    }
};

// 派生类
class Cat : public Animal {
public:
    Cat(String n) : Animal(n) {}
    
    // 重写基类方法
    void eat() {
        Serial.printf("%s 在吃鱼\n", name.c_str());
    }
    
    // 派生类特有方法
    void meow() {
        Serial.println("喵~");
    }
};

// 使用
Cat myCat("咪咪");
myCat.eat();    // 咪咪在吃鱼
myCat.meow();   // 喵~
```

### 枚举类

```cpp
// 传统枚举
enum CatExpression {
    CAT_NEUTRAL = 0,
    CAT_HAPPY,
    CAT_THINKING,
    CAT_SLEEPY,
    CAT_COUNT
};

// C++11 枚举类（类型安全）
enum class Emotion : uint8_t {
    Neutral = 0,
    Happy,
    Thinking,
    Sleepy
};

// 使用
CatExpression expr = CAT_HAPPY;
Emotion emo = Emotion::Happy;
```

---

## 1.4.3 内存管理与指针

### 指针基础

```cpp
int value = 42;
int* ptr = &value;      // ptr 指向 value 的地址

Serial.println(value);   // 42 - 直接访问
Serial.println(*ptr);    // 42 - 通过指针解引用
Serial.println(ptr);     // 0x3ffd0000 - 地址值
Serial.println(&value);  // 0x3ffd0000 - value 的地址

// 修改指针指向的值
*ptr = 100;
Serial.println(value);   // 100
```

### 动态内存分配

```cpp
// 分配内存
int* numbers = (int*)malloc(10 * sizeof(int));

// 使用内存
numbers[0] = 1;
numbers[9] = 10;

// 释放内存（重要！）
free(numbers);
numbers = NULL;   // 避免悬空指针

// C++ 风格的 new/delete
int* singleValue = new int;
*singleValue = 42;
delete singleValue;

// 数组
int* array = new int[100];
delete[] array;
```

### ESP32 内存区域

ESP32-S3 有多种内存区域：

| 内存类型 | 大小 | 用途 | 分配方式 |
|---------|------|------|---------|
| SRAM | 512KB | 内部高速 RAM | 默认分配 |
| PSRAM | 8MB | 外部大容量 RAM | `ps_malloc()` |
| Flash | 16MB | 存储程序和常量 | 编译时确定 |

```cpp
// 从 PSRAM 分配大缓冲区
uint8_t* buffer = (uint8_t*)ps_malloc(100000);

// 检查分配是否成功
if (buffer == NULL) {
    Serial.println("内存分配失败");
}

// 使用后释放
free(buffer);
```

### 内存泄漏防范

```cpp
// 错误示例：分配后忘记释放
void badFunction() {
    int* data = new int[1000];
    // ... 使用 data
    // 忘记 delete[] data - 内存泄漏！
}

// 正确示例
void goodFunction() {
    int* data = new int[1000];
    // ... 使用 data
    delete[] data;  // 释放内存
}

// 更好的方式：使用智能指针或容器
#include <vector>
std::vector<int> data(1000);  // 自动管理内存
```

---

## 1.4.4 Arduino 框架基础

### Arduino 程序结构

```cpp
// setup() - 初始化函数，只执行一次
void setup() {
    Serial.begin(115200);     // 初始化串口
    pinMode(LED_BUILTIN, OUTPUT);  // 设置引脚模式
    WiFi.begin(ssid, password);    // 连接 WiFi
}

// loop() - 主循环函数，不断重复
void loop() {
    // 主逻辑
    digitalWrite(LED_BUILTIN, HIGH);
    delay(1000);
    digitalWrite(LED_BUILTIN, LOW);
    delay(1000);
}
```

### 常用 Arduino API

**GPIO 控制：**
```cpp
// 设置引脚模式
pinMode(pin, INPUT);       // 输入
pinMode(pin, OUTPUT);      // 输出
pinMode(pin, INPUT_PULLUP);// 输入带上拉

// 数字读写
digitalWrite(pin, HIGH);   // 输出高电平
digitalWrite(pin, LOW);    // 输出低电平
int value = digitalRead(pin);  // 读取数字输入
```

**模拟输入：**
```cpp
// 读取模拟值（0-4095 for ESP32）
int sensorValue = analogRead(GPIO36);
```

**时间函数：**
```cpp
unsigned long start = millis();   // 获取运行毫秒数
delay(1000);                      // 延时 1 秒
delayMicroseconds(500);           // 延时 500 微秒
```

**串口通信：**
```cpp
Serial.begin(115200);             // 初始化串口
Serial.println("Hello");          // 打印一行
Serial.printf("Value: %d\n", x);  // 格式化打印
int data = Serial.read();         // 读取数据
```

### FreeRTOS 基础

ESP32 运行 FreeRTOS 实时操作系统：

```cpp
// 创建任务
void taskFunction(void* parameter) {
    while (true) {
        // 任务逻辑
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

// 启动任务
xTaskCreate(
    taskFunction,           // 任务函数
    "Task1",                // 任务名称
    4096,                   // 栈大小
    NULL,                   // 参数
    5,                      // 优先级
    NULL                    // 任务句柄
);

// 绑定任务到特定核心
xTaskCreatePinnedToCore(
    taskFunction,
    "Task1",
    4096,
    NULL,
    5,
    NULL,
    1         // Core 1
);
```

---

## 本章小结

本章学习了 C++ 语言基础：
- ✅ C++ 基础语法（变量、运算符、控制结构）
- ✅ 类与对象（定义、继承、枚举）
- ✅ 内存管理与指针（动态分配、ESP32 内存）
- ✅ Arduino 框架基础（API、FreeRTOS）

## 下一步

继续学习 **[1.5 数据结构基础](05-数据结构.md)**，掌握编程中常用的数据结构。

---

## 实践练习

1. 编写一个函数，计算两个整数的和、差、积、商
2. 创建一个 `Student` 类，包含姓名、年龄、成绩属性
3. 使用动态数组存储 100 个整数，并计算平均值
4. 创建一个 FreeRTOS 任务，每秒打印一次当前时间

## 常见问题

**Q: `int` 和 `long` 在 ESP32 上有什么区别？**  
A: 在 ESP32 上，`int` 和 `long` 都是 32 位（4 字节）。`long long` 是 64 位。

**Q: 什么时候用 `malloc` 什么时候用 `new`？**  
A: `malloc/free` 是 C 风格，`new/delete` 是 C++ 风格。C++ 代码推荐用 `new/delete`，或更好的智能指针。

**Q: 为什么我的 ESP32 经常重启？**  
A: 可能是内存不足或看门狗超时。检查是否有内存泄漏，或长时间阻塞操作。

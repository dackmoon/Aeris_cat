# 1.8 Python 后端基础

> 最后更新：2026-04-11  
> 适用版本：Cyber Cat Phase 4/5

---

## 本章目标

完成本章后，你将：
- [ ] 掌握 Python 3.12 基础语法
- [ ] 理解 FastAPI 框架和路由
- [ ] 掌握异步编程 (async/await)
- [ ] 理解 HTTP 协议和 RESTful API 设计

---

## 1.8.1 Python 3.12 基础

### 安装 Python

**Windows/macOS：**
访问 https://www.python.org/downloads/ 下载 Python 3.12+

**Linux (Ubuntu)：**
```bash
sudo apt update
sudo apt install python3.12 python3.12-pip python3.12-venv
python3.12 --version
```

### 变量与数据类型

```python
# 变量声明（不需要类型声明）
name = "Cyber Cat"
age = 3
price = 19.99
is_active = True

# 查看类型
print(type(name))  # <class 'str'>
print(type(age))   # <class 'int'>

# 基本数据类型
# str - 字符串
text = "Hello"
message = 'World'
multi_line = """多行
字符串"""

# int - 整数
count = 42
negative = -10

# float - 浮点数
pi = 3.14159
temperature = 36.5

# bool - 布尔值
is_valid = True
is_error = False

# None - 空值
result = None
```

### 容器类型

```python
# List - 列表（有序，可变）
fruits = ["apple", "banana", "orange"]
fruits.append("grape")
print(fruits[0])  # apple
print(len(fruits))  # 4

# Tuple - 元组（有序，不可变）
point = (10, 20)
x, y = point  # 解包

# Dict - 字典（键值对）
person = {
    "name": "Alice",
    "age": 25,
    "city": "Beijing"
}
print(person["name"])  # Alice
person["email"] = "alice@example.com"

# Set - 集合（无序，唯一）
unique_numbers = {1, 2, 3, 3, 4}
print(unique_numbers)  # {1, 2, 3, 4}
```

### 控制结构

```python
# 条件语句
score = 85

if score >= 90:
    print("优秀")
elif score >= 60:
    print("及格")
else:
    print("不及格")

# 循环 - for
for i in range(5):
    print(i)  # 0, 1, 2, 3, 4

for fruit in ["apple", "banana"]:
    print(fruit)

# 循环 - while
count = 0
while count < 5:
    print(count)
    count += 1

# 循环控制
for i in range(10):
    if i == 3:
        continue  # 跳过本次
    if i == 7:
        break  # 退出循环
    print(i)
```

### 函数

```python
# 基础函数
def greet(name):
    return f"Hello, {name}!"

result = greet("Alice")
print(result)  # Hello, Alice!

# 默认参数
def power(base, exp=2):
    return base ** exp

print(power(3))    # 9
print(power(3, 3)) # 27

# 可变参数
def sum_all(*args):
    return sum(args)

print(sum_all(1, 2, 3, 4))  # 10

# 关键字参数
def create_user(**kwargs):
    return kwargs

user = create_user(name="Alice", age=25)
# {'name': 'Alice', 'age': 25}
```

### 类与对象

```python
class Cat:
    # 类变量
    species = "Felis catus"
    
    def __init__(self, name, age):
        # 实例变量
        self.name = name
        self.age = age
    
    # 实例方法
    def meow(self):
        return f"{self.name} 说：喵~"
    
    # 特殊方法（字符串表示）
    def __str__(self):
        return f"Cat({self.name}, {self.age}岁)"

# 创建对象
my_cat = Cat("咪咪", 3)
print(my_cat.name)    # 咪咪
print(my_cat.meow())  # 咪咪 说：喵~
print(my_cat)         # Cat(咪咪, 3 岁)

# 继承
class SmartCat(Cat):
    def think(self):
        return f"{self.name} 在思考..."

smart = SmartCat("博士", 5)
print(smart.meow())   # 继承自父类
print(smart.think())  # 博士 在思考...
```

---

## 1.8.2 FastAPI 框架

### FastAPI 简介

**FastAPI 是什么？** FastAPI 是一个现代、快速的 Python Web 框架，特点：
- 高性能（与 Node.js 和 Go 相当）
- 自动生成 OpenAPI 文档
- 基于类型提示
- 异步支持

### 安装 FastAPI

```bash
# 创建虚拟环境
python3 -m venv venv
source venv/bin/activate  # Linux/macOS
# 或 venv\Scripts\activate  # Windows

# 安装依赖
pip install fastapi uvicorn httpx python-dotenv
```

### 第一个 FastAPI 应用

```python
# main.py
from fastapi import FastAPI

app = FastAPI(
    title="Cyber Cat API",
    description="AI 猫咪后端服务",
    version="1.0.0"
)

# 根路由
@app.get("/")
async def root():
    return {"message": "Hello, Cyber Cat!"}

# 带参数的路由
@app.get("/items/{item_id}")
async def read_item(item_id: int):
    return {"item_id": item_id}

# 查询参数
@app.get("/search")
async def search(q: str, limit: int = 10):
    return {"query": q, "limit": limit}
```

**运行应用：**
```bash
uvicorn main:app --reload --host 0.0.0.0 --port 8000
```

**访问文档：**
- Swagger UI: http://localhost:8000/docs
- ReDoc: http://localhost:8000/redoc

### 请求体（Pydantic 模型）

```python
from fastapi import FastAPI
from pydantic import BaseModel

app = FastAPI()

# 定义请求模型
class ChatRequest(BaseModel):
    text: str
    session_id: str | None = None

class ChatResponse(BaseModel):
    reply: str
    emotion: str

@app.post("/chat", response_model=ChatResponse)
async def chat(request: ChatRequest):
    # 处理逻辑
    reply = f"收到：{request.text}"
    return ChatResponse(reply=reply, emotion="neutral")
```

### 文件上传

```python
from fastapi import FastAPI, File, UploadFile, Header
from typing import Optional

app = FastAPI()

@app.post("/upload")
async def upload_audio(
    file: UploadFile = File(...),
    x_session_id: Optional[str] = Header(None)
):
    # 读取文件内容
    contents = await file.read()
    
    # 处理音频
    # ...
    
    return {
        "filename": file.filename,
        "size": len(contents),
        "session_id": x_session_id
    }
```

### 项目实际应用 - Cyber Cat 后端

```python
# main.py
from fastapi import FastAPI, Request, Header
from fastapi.responses import JSONResponse
from services import stt, llm, tts, emotion
from memory import repository

app = FastAPI(title="Cyber Cat Backend")

@app.post("/chat")
async def chat(
    request: Request,
    x_session_id: str | None = Header(None)
):
    """
    主聊天接口：接收音频，返回回复和 TTS 音频
    
    - **x_session_id**: 设备会话 ID（可选）
    """
    # 读取音频数据
    wav_bytes = await request.body()
    
    # 1. STT：语音转文字
    transcript = await stt.transcribe(wav_bytes)
    
    # 2. 获取记忆
    session_id = x_session_id or "default"
    memory_summary = repository.get_memory_summary(session_id)
    session_history = repository.get_session_history(session_id)
    
    # 3. LLM：生成回复
    reply = await llm.chat_completion(
        transcript, 
        memory_summary,
        session_history
    )
    
    # 4. 情绪分析
    emo = emotion.analyze(reply)
    
    # 5. TTS：生成音频
    audio_url = await tts.generate_speech(reply)
    
    # 6. 保存对话到记忆
    repository.save_interaction(session_id, transcript, reply)
    
    return {
        "text_reply": reply,
        "emotion": emo,
        "audio_url": audio_url
    }

@app.get("/health")
async def health_check():
    """健康检查接口"""
    return {"status": "ok", "version": "1.0.0"}
```

---

## 1.8.3 异步编程 (async/await)

### 同步 vs 异步

**同步代码：**
```python
import time

def task1():
    print("任务 1 开始")
    time.sleep(2)  # 阻塞 2 秒
    print("任务 1 完成")

def task2():
    print("任务 2 开始")
    time.sleep(2)  # 阻塞 2 秒
    print("任务 2 完成")

# 顺序执行，总共需要 4 秒
task1()
task2()
```

**异步代码：**
```python
import asyncio

async def task1():
    print("任务 1 开始")
    await asyncio.sleep(2)  # 非阻塞 2 秒
    print("任务 1 完成")

async def task2():
    print("任务 2 开始")
    await asyncio.sleep(2)  # 非阻塞 2 秒
    print("任务 2 完成")

# 并发执行，总共需要 2 秒
async def main():
    await asyncio.gather(task1(), task2())

asyncio.run(main())
```

### async/await 基础

```python
import asyncio

# 异步函数
async def fetch_data(url: str) -> str:
    print(f"开始请求：{url}")
    await asyncio.sleep(1)  # 模拟网络请求
    return f"数据：{url}"

# 等待异步结果
async def main():
    result = await fetch_data("https://api.example.com")
    print(result)

asyncio.run(main())
```

### 并发执行

```python
import asyncio

async def fetch(url: str, delay: int):
    await asyncio.sleep(delay)
    return f"{url} - {delay}s"

async def main():
    # 顺序执行（3 秒）
    result1 = await fetch("A", 1)
    result2 = await fetch("B", 2)
    
    # 并发执行（2 秒）
    results = await asyncio.gather(
        fetch("A", 1),
        fetch("B", 2),
        fetch("C", 3)
    )
    print(results)  # ['A', 'B', 'C']

asyncio.run(main())
```

### 项目实际应用 - 并发 API 调用

```python
import httpx
import asyncio

async def call_stt(wav_bytes: bytes) -> str:
    """异步调用 STT API"""
    async with httpx.AsyncClient() as client:
        response = await client.post(
            "https://api.siliconflow.cn/v1/audio/transcriptions",
            headers={"Authorization": f"Bearer {API_KEY}"},
            files={"file": ("audio.wav", wav_bytes, "audio/wav")},
            data={"model": "FunAudioLLM/SenseVoiceSmall"}
        )
        return response.json()["text"]

async def call_llm(messages: list) -> str:
    """异步调用 LLM API"""
    async with httpx.AsyncClient() as client:
        response = await client.post(
            "https://api.dashscope.aliyun.com/v1/chat/completions",
            headers={"Authorization": f"Bearer {API_KEY}"},
            json={"model": "MiniMax-M2.5", "messages": messages}
        )
        return response.json()["choices"][0]["message"]["content"]

async def process_chat(wav_bytes: bytes):
    """并发处理聊天请求"""
    # STT 和 LLM 可以部分并行
    transcript = await call_stt(wav_bytes)
    reply = await call_llm([{"role": "user", "content": transcript}])
    return reply

# 运行
asyncio.run(process_chat(audio_data))
```

---

## 1.8.4 HTTP 协议基础

### HTTP 请求结构

```
HTTP 请求 = 请求行 + 请求头 + 请求体

示例：
POST /chat HTTP/1.1
Host: 122.51.129.89:8000
Content-Type: audio/wav
Content-Length: 143084

[二进制音频数据]
```

### HTTP 方法

| 方法 | 用途 | 幂等 |
|------|------|------|
| GET | 获取资源 | 是 |
| POST | 创建资源 | 否 |
| PUT | 更新资源 | 是 |
| DELETE | 删除资源 | 是 |
| PATCH | 部分更新 | 否 |

### HTTP 状态码

| 状态码 | 含义 |
|--------|------|
| 200 OK | 成功 |
| 201 Created | 创建成功 |
| 204 No Content | 成功，无返回内容 |
| 400 Bad Request | 请求格式错误 |
| 401 Unauthorized | 未授权 |
| 403 Forbidden | 禁止访问 |
| 404 Not Found | 资源不存在 |
| 500 Internal Server Error | 服务器内部错误 |

### 使用 httpx 发送请求

```python
import httpx
import asyncio

async def http_examples():
    async with httpx.AsyncClient() as client:
        
        # GET 请求
        response = await client.get("https://api.github.com")
        print(response.status_code)
        print(response.json())
        
        # POST 请求（JSON）
        response = await client.post(
            "https://api.example.com/chat",
            json={"text": "hello"}
        )
        
        # POST 请求（文件上传）
        with open("audio.wav", "rb") as f:
            response = await client.post(
                "https://api.example.com/upload",
                files={"file": ("audio.wav", f, "audio/wav")}
            )
        
        # 带请求头
        response = await client.get(
            "https://api.example.com/data",
            headers={"Authorization": "Bearer token123"}
        )

asyncio.run(http_examples())
```

### RESTful API 设计

**资源命名：**
```
# 好的设计
GET    /users          # 获取用户列表
GET    /users/123      # 获取用户 123
POST   /users          # 创建用户
PUT    /users/123      # 更新用户 123
DELETE /users/123      # 删除用户 123

# 不好的设计
GET    /getUsers
POST   /createUser
```

**项目 API 设计：**
```python
# Cyber Cat API 端点

# 主聊天接口
POST /chat
Content-Type: audio/wav
X-Session-ID: device-001

Response:
{
    "text_reply": "喵~听到了！",
    "emotion": "happy",
    "audio_url": "http://server/tts/xxx.wav"
}

# 健康检查
GET /health
Response: {"status": "ok"}

# 设备管理
GET /devices
Response: [{"device_id": "001", "name": "小喵一号"}]

# 记忆查询
GET /memory/{session_id}
Response: {"summary": "用户喜欢猫", "interactions": [...]}
```

---

## 本章小结

本章学习了 Python 后端基础：
- ✅ Python 3.12 基础语法和数据类型
- ✅ FastAPI 框架和路由
- ✅ 异步编程 (async/await)
- ✅ HTTP 协议和 RESTful API 设计

## 下一步

继续学习 **[1.9 通信协议基础](09-通信协议.md)**，深入理解设备间通信。

---

## 实践练习

1. 创建一个 FastAPI 应用，实现 `/add` 接口计算两个数的和
2. 编写异步函数，并发请求 3 个 API 并合并结果
3. 实现文件上传接口，保存上传的音频文件
4. 使用 httpx 调用第三方 API（如天气 API）

## 常见问题

**Q: async 和 await 必须一起使用吗？**  
A: 是的，`await` 只能在 `async def` 函数内使用。`async` 定义异步函数，`await` 等待异步结果。

**Q: FastAPI 和 Flask 有什么区别？**  
A: FastAPI 原生支持异步、自动生成文档、基于类型提示。Flask 更简单但功能较少。

**Q: 如何处理异步错误？**  
A: 使用 `try/except` 包裹 `await` 调用，或用 `asyncio.create_task()` + `try/except` 处理后台任务。

@echo off
echo ESP8266 Chat & Image Transfer Application
echo =========================================
echo.

:: Check if Python is installed
python --version >nul 2>&1
if %errorlevel% neq 0 (
    echo Python is not installed or not in your PATH.
    echo Please install Python 3.6 or newer from https://www.python.org/downloads/
    echo Make sure to check "Add Python to PATH" during installation.
    pause
    exit /b 1
)

:: Check and install required packages
echo Checking and installing required packages...
python -m pip install --upgrade pip >nul 2>&1
python -m pip install pyserial pillow >nul 2>&1

:: Run the application
echo Starting ESP Chat Application...
echo.
python esp_chat_app.py

:: If there was an error, pause to show the error message
if %errorlevel% neq 0 (
    echo.
    echo An error occurred while running the application.
    pause
) 
from machine import Pin
import time

led = Pin(2, Pin.OUT)  # built-in LED (GPIO2 on many boards)

while True:
    led.value(not led.value())
    time.sleep(0.5)
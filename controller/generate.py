from os import system
from PIL import Image, ImageDraw, ImageFont

import RPi.GPIO as GPIO
import time

# map our pins to the hardware
pin_1 = 17
pin_2 = 22
pin_3 = 23
pin_4 = 27

# These are shared memory locations for state management
# used by the button callback handler to write
display_page = pin_1  # display page 1 by default
backlight = 1000  # set backlight to brightest by default


def button_callback(channel):
    print(str(channel) + "Button pressed")

    # we use button 4 to set the backlight level
    if channel == pin_4:
        global backlight
        if backlight == 0:
            backlight = 1000
        else:
            backlight -= 100

    # otherwise just remember the page to display
    else:
        global display_page
        display_page = channel


def backlight(level):
    print("turning backlight " + str(level))
    command = "gpio -g pwm 18 " + str(level)
    system(command)


def render_temp(temp):

    # get a font
    fnt_file = "/usr/share/fonts/truetype/noto/NotoMono-Regular.ttf"
    fnt = ImageFont.truetype(fnt_file, 30)

    if temp >= 23 and temp <= 25:
        out = Image.new("RGB", (320, 240), "DarkGreen")  # green background
        d = ImageDraw.Draw(out)
        message = str(temp) + "C\nSafe"
        d.multiline_text((10, 10), message, font=fnt, fill=(0, 0, 0))

    elif (temp >= 21 and temp < 23) or (temp <= 27 and temp > 25):
        out = Image.new("RGB", (150, 100), (100, 100, 0))  # amber background
        d = ImageDraw.Draw(out)
        d.multiline_text((10, 10), str(temp) + "C\nWarn", font=fnt, fill=(0, 0, 0))

    else:
        out = Image.new("RGB", (150, 100), (100, 0, 0))  # red background
        d = ImageDraw.Draw(out)
        d.multiline_text((10, 10), str(temp) + "C\nCritical", font=fnt, fill=(0, 0, 0))


def make_page(page):

    if page == pin_1:
        temp = 23  # really call for temp here
        out = render_temp(temp)
        out.save("display" + str(pin_1) + "jpg", "JPEG")
        system("sudo fbi -T 2 -d /dev/fb1 -noverbose -a display" + str(pin_1) + ".jpg")


GPIO.setwarnings(False)  # Ignore warning for now
GPIO.setmode(GPIO.BCM)

# Setup all of the buttons
GPIO.setup(pin_1, GPIO.IN, pull_up_down=GPIO.PUD_UP)
GPIO.setup(pin_2, GPIO.IN, pull_up_down=GPIO.PUD_UP)
GPIO.setup(pin_3, GPIO.IN, pull_up_down=GPIO.PUD_UP)
GPIO.setup(pin_4, GPIO.IN, pull_up_down=GPIO.PUD_UP)

# set our callback for all the buttons
GPIO.add_event_detect(pin_1, GPIO.FALLING, callback=button_callback)
GPIO.add_event_detect(pin_2, GPIO.FALLING, callback=button_callback)
GPIO.add_event_detect(pin_3, GPIO.FALLING, callback=button_callback)
GPIO.add_event_detect(pin_4, GPIO.FALLING, callback=button_callback)

# init pwm for the backlight control
system("gpio -g mode 18 pwm")

while True:
    make_page(display_page)
    time.sleep(1)

GPIO.cleanup()  # Clean up

import subprocess
import RPi.GPIO as GPIO
from time import sleep

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
            backlight -= 200
        subprocess.run(["gpio", "-g", "pwm", "18", str(backlight)])
    # otherwise just remember the page to display
    else:
        global display_page
        display_page = channel


def render_temp(temp):

    if temp >= 23 and temp <= 25:
        bg_color = "DarkGreen"
        message = "Probe 1: " + str(temp) + "C\n\n" \
                  "Probe 2: " + str(temp) + "C\n"

    elif (temp >= 21 and temp < 23) or (temp <= 27 and temp > 25):
        bg_color = "DarkOrange"
        message = str(temp) + "C\nWarn"

    else:
        bg_color = "DarkRed"
        message = str(temp) + "C\nCritical"

    subprocess.run(["convert", "-background", bg_color,
                    "-size", "320x240",
                    "-fill", "black",
                    "-pointsize", "48",
                    "-font", "URW-Gothic-L-Demi-Oblique",
                    "label:" + message,
                    "display" + str(pin_1) + ".jpg"])
    subprocess.run(["sudo", "fbi", "-T", "2", "-d", "/dev/fb1", "-noverbose",
                   "-a", "display" + str(pin_1) + ".jpg"],
                   stdout=subprocess.DEVNULL)


def make_page(page):

    if page == pin_1:
        temp = 23  # really call for temp here
        render_temp(temp)


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
subprocess.run(["gpio", "-g", "mode", "18", "pwm"])

while True:
    make_page(display_page)
    sleep(2)

GPIO.cleanup()  # Clean up

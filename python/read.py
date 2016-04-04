import serial

ser = serial.Serial('/dev/ttyUSB0',9600, timeout=1)

ser.write('D')
while True:
    print ser.readline(),

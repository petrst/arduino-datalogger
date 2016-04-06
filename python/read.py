import serial
from datetime import datetime

ser = serial.Serial('/dev/ttyUSB0',9600, timeout=1)

end=False
ser.write('D')
while not end:
    line=ser.readline()
    if line.startswith('END'):
	end=True
    print line,

now = datetime.now()
now_f = now.strftime("%Y%m%d%H%M%S")
print "SYNCING",now_f
ser.write('T')
ser.write(now_f)
print ser.readline()

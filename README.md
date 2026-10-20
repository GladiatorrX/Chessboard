# Chessboard
Physical chessboard coded to display bot moves, so you can play against bots on a real board

The board works by detecting the squares on the boards and using them as inputs. PLayer moves are tracked and sent to Stockfish via Lichess.org over a Wifi connection. Bot Moves are received similarly. 

I used an ESP32 for this project, and a 0.96 inch OLED. I made a custom board which was a detectable 8x8 matrix using conductive strips. 
To make it, on a flat board glue down 8 horizontal strips of either copper tape or aluminum foil.
Put a piece of card paper over it with square holes in it just below where the actual boards intended squares will be.
Next, put vertical strips over the piece of paper, and cover these 3 layers with a paper showing the chessboard. This will be your board top.

          //Pin mapping:
Column a: 13        Row 1: 16 (RX2 pin)
Column b: 14        Row 2: 17 (TX2 pin)
Column c: 25        Row 3: 18
Column d: 26        Row 4: 19
Column e: 27        Row 5: 21
Column f: 33        Row 6: 22
Column g: 4         Row 7: 23
Column h: 2         Row 8: 32

OLED Screen:        Power:
SDA: 5              Battery positive to VIN
SCL: 15             Battery Negative: GND
VCC: 3v3
GND: GND

Now, the last thing you need to do is make a free Lichess.org account and generate a token. 
Enter the token along with your wifi credentials into the begining of the code, and you're all set to start playing!

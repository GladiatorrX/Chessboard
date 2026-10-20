#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

const char* WIFI_SSID     = " ";
const char* WIFI_PASSWORD = " ";
const char* LICHESS_TOKEN = " ";   //enter your own credentials here

const int colPins[8] = {13, 14, 25, 26, 27, 33, 4, 2};   
const int rowPins[8] = {16, 17, 18, 19, 21, 22, 23, 32}; 
const char colNames[8] = {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h'};
const char rowNames[8] = {'1', '2', '3', '4', '5', '6', '7', '8'};

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define I2C_SDA 5
#define I2C_SCL 15
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

char board[8][8]; 

WebServer server(80);
String webLine1 = "Booting...";
String webLine2 = "Connecting...";

enum GameState {
  CHOOSE_DIFFICULTY,
  CHOOSE_SIDE,
  PLAYER_START_SQUARE,
  PLAYER_END_SQUARE,
  WAITING_FOR_BOT_PHYSICAL_MOVE,
  GAME_OVER
};

GameState currentState = CHOOSE_DIFFICULTY;
int difficultyLevel = 2;
bool playingWhite = true;

String currentGameId = "";
int knownMoveCount = 0;

String startCoord = "";
String endCoord = "";
String movingPieceName = "";

String expectedBotStart = "";
String expectedBotEnd = "";
String botPieceName = "";
bool botPieceLifted = false;

void printScreen(String line1, String line2);
String scanMatrix();
void waitForRelease(int colIndex, int rowIndex);
void initVirtualBoard();
bool isLegalLocalMove(String sSq, String eSq);
void applyMoveToBoard(String uciMove);
bool createLichessAIGame();
bool sendMoveToLichess(String uciMove);
bool resignLichessGame();
void waitForBotResponse();
String getPieceName(char p);
String extractJsonString(String json, String key);
int countMoves(String movesStr);
String getLastMove(String movesStr);

void setup() {
  Serial.begin(115200);
  delay(1000);

  for (int c = 0; c < 8; c++) {
    pinMode(colPins[c], OUTPUT);
    digitalWrite(colPins[c], HIGH);
  }
  for (int r = 0; r < 8; r++) {
    pinMode(rowPins[r], INPUT_PULLUP);
  }

  Wire.begin(I2C_SDA, I2C_SCL);
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
    Serial.println(F("OLED failed"));
  }
  display.clearDisplay();
  display.display();

  Serial.println("\n--- CHESSBOARD BOOTING ---");
  printScreen("BOOTING UP", "Connecting...");

  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWi-Fi Connected!");

  server.on("/", []() {
    String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1.0'><meta http-equiv='refresh' content='1'></head>";
    html += "<body style='background-color:#121212; color:#ffffff; font-family:sans-serif; text-align:center; padding-top:20%; margin:0;'>";
    html += "<h1 style='font-size:2.5rem;'>" + webLine1 + "</h1>";
    html += "<p style='font-size:1.5rem; color:#00ff00; padding: 0 20px; font-weight:bold;'>" + webLine2 + "</p>";
    html += "</body></html>";
    server.send(200, "text/html", html);
  });
  server.begin();

  printScreen("DIFFICULTY", "Row 3: Lvl 1-8");
}

void loop() {
  server.handleClient(); 
  String pressedSquare = scanMatrix();

  if (pressedSquare != "") {
    switch (currentState) {

      case CHOOSE_DIFFICULTY:
        if (pressedSquare[1] == '3' || pressedSquare[1] == '6') {
          difficultyLevel = pressedSquare[0] - 'a' + 1; 
          printScreen("CHOOSE SIDE", "e4=W | e5=B");
          currentState = CHOOSE_SIDE;
          delay(500); 
        }
        break;

      case CHOOSE_SIDE:
        if (pressedSquare == "e4" || pressedSquare == "e5") {
          playingWhite = (pressedSquare == "e4");
          printScreen("STARTING", "Connecting...");
          
          initVirtualBoard();
          if (createLichessAIGame()) {
            if (playingWhite) {
              printScreen("YOUR MOVE", "Select piece");
              currentState = PLAYER_START_SQUARE;
            } else {
              printScreen("WAITING...", "I play White");
              waitForBotResponse();
            }
            delay(500); 
          } else {
            printScreen("API ERROR", "Check WiFi");
            delay(2000);
            currentState = GAME_OVER;
          }
        }
        break;

      case PLAYER_START_SQUARE: {
        int c = pressedSquare[0] - 'a';
        int r = pressedSquare[1] - '1';
        char p = board[c][r];

        bool isOwnPiece = (p != '.') && (playingWhite ? isUpperCase(p) : isLowerCase(p));
        if (!isOwnPiece) {
          printScreen("INVALID!", "Not your piece");
          break;
        }

        startCoord = pressedSquare;
        movingPieceName = getPieceName(p);
        
        printScreen(movingPieceName + " TO?", startCoord + "-??");
        currentState = PLAYER_END_SQUARE;
        break;
      }

      case PLAYER_END_SQUARE: {
        endCoord = pressedSquare;

        if (startCoord == endCoord) {
          printScreen("CANCELLED", "Select piece");
          currentState = PLAYER_START_SQUARE;
          break;
        }
        
        char startPiece = board[startCoord[0]-'a'][startCoord[1]-'1'];
        char endPiece = board[endCoord[0]-'a'][endCoord[1]-'1'];
        
        if (toupper(startPiece) == 'K' && toupper(endPiece) == 'K') {
          printScreen("RESIGNING", "Sending...");
          resignLichessGame();
          printScreen("GAME OVER", "You Resigned");
          currentState = GAME_OVER;
          delay(500);
          break;
        }

        if (!isLegalLocalMove(startCoord, endCoord)) {
          printScreen("ILLEGAL!", "Return " + startCoord);
          currentState = PLAYER_START_SQUARE;
          delay(500);
          break;
        }

        String uciMove = startCoord + endCoord;
        if (toupper(board[startCoord[0]-'a'][startCoord[1]-'1']) == 'P' && (endCoord[1] == '8' || endCoord[1] == '1')) {
          uciMove += "q"; 
        }

        printScreen("YOU MOVED:", startCoord + "-" + endCoord);

        if (sendMoveToLichess(uciMove)) {
          applyMoveToBoard(uciMove);
          knownMoveCount++;
          delay(200); 
          waitForBotResponse();
        } else {
          printScreen("ILLEGAL!", "Return " + startCoord);
          currentState = PLAYER_START_SQUARE;
          delay(500);
        }
        break;
      }

      case WAITING_FOR_BOT_PHYSICAL_MOVE:
        if (!botPieceLifted) {
          if (pressedSquare == expectedBotStart) {
            botPieceLifted = true;
            printScreen("PLACE ON:", expectedBotEnd);
          } else {
            printScreen("WRONG SQUARE", "Lift " + expectedBotStart);
          }
        } else {
          if (pressedSquare == expectedBotEnd) {
            applyMoveToBoard(expectedBotStart + expectedBotEnd);
            printScreen("YOUR MOVE", "Select piece");
            currentState = PLAYER_START_SQUARE;
            delay(500);
          } else {
            printScreen("WRONG SQUARE", "Place on " + expectedBotEnd);
          }
        }
        break;

      case GAME_OVER:
        if (pressedSquare == "e4") {
          printScreen("DIFFICULTY", "Row 3 = Lvl 1-8");
          currentState = CHOOSE_DIFFICULTY;
          delay(500);
        }
        break;
    }
  }
}

bool createLichessAIGame() {
  WiFiClientSecure client;
  client.setInsecure(); 
  HTTPClient https;

  if (!https.begin(client, "https://lichess.org/api/challenge/ai")) return false;

  https.addHeader("Authorization", String("Bearer ") + LICHESS_TOKEN);
  https.addHeader("Content-Type", "application/x-www-form-urlencoded");

  String payload = "level=" + String(difficultyLevel) + "&color=" + (playingWhite ? "white" : "black");
  int httpCode = https.POST(payload);
  String response = https.getString();
  https.end();

  if (httpCode == 201 || httpCode == 200) {
    currentGameId = extractJsonString(response, "id");
    knownMoveCount = 0;
    return true;
  }
  return false;
}

bool sendMoveToLichess(String uciMove) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;

  String url = "https://lichess.org/api/board/game/" + currentGameId + "/move/" + uciMove;
  if (!https.begin(client, url)) return false;

  https.addHeader("Authorization", String("Bearer ") + LICHESS_TOKEN);
  int httpCode = https.POST("");
  https.end();

  return (httpCode == 200); 
}

bool resignLichessGame() {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;

  String url = "https://lichess.org/api/board/game/" + currentGameId + "/resign";
  if (!https.begin(client, url)) return false;

  https.addHeader("Authorization", String("Bearer ") + LICHESS_TOKEN);
  int httpCode = https.POST("");
  https.end();

  return (httpCode == 200);
}

void waitForBotResponse() {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;

  String url = "https://lichess.org/api/board/game/stream/" + currentGameId;
  if (!https.begin(client, url)) return;

  https.addHeader("Authorization", String("Bearer ") + LICHESS_TOKEN);
  int httpCode = https.GET();

  if (httpCode == 200) {
    WiFiClient* stream = https.getStreamPtr();
    unsigned long timeout = millis();

    while (https.connected() && (millis() - timeout < 20000)) { 
      server.handleClient(); 
      
      if (stream->available()) {
        String line = stream->readStringUntil('\n');
        if (line.length() == 0) continue;

        String movesStr = extractJsonString(line, "moves");
        String status = extractJsonString(line, "status");

        int totalMoves = countMoves(movesStr);

        if (totalMoves > knownMoveCount) {
          knownMoveCount = totalMoves;
          String lastMove = getLastMove(movesStr);
          expectedBotStart = lastMove.substring(0, 2);
          expectedBotEnd   = lastMove.substring(2, 4);
          
          botPieceName = getPieceName(board[expectedBotStart[0]-'a'][expectedBotStart[1]-'1']);
          botPieceLifted = false;

          if (status == "mate" || status == "draw" || status == "stalemate") {
            printScreen("GAME OVER", status);
            currentState = GAME_OVER;
            https.end();
            return;
          }

          printScreen("I PLAY " + botPieceName + ":", expectedBotStart + "-" + expectedBotEnd);
          currentState = WAITING_FOR_BOT_PHYSICAL_MOVE;
          https.end();
          return;
        }
        
        if (status == "mate" || status == "draw" || status == "stalemate" || status == "resign" || status == "aborted") {
            printScreen("GAME OVER", status);
            currentState = GAME_OVER;
            https.end();
            return;
        }
      }
      delay(20);
    }
  }
  https.end();
}

void waitForRelease(int colIndex, int rowIndex) {
  digitalWrite(colPins[colIndex], LOW);
  delayMicroseconds(20);
  while (digitalRead(rowPins[rowIndex]) == LOW) { 
    server.handleClient(); 
    delay(10); 
  }
  delay(60); 
  digitalWrite(colPins[colIndex], HIGH);
}

String scanMatrix() {
  for (int c = 0; c < 8; c++) {
    digitalWrite(colPins[c], LOW);
    delayMicroseconds(500); 
    for (int r = 0; r < 8; r++) {
      if (digitalRead(rowPins[r]) == LOW) {
        delay(5); 
        if (digitalRead(rowPins[r]) == LOW) {
          String sq = String(colNames[c]) + String(rowNames[r]);
          waitForRelease(c, r);
          return sq;
        }
      }
    }
    digitalWrite(colPins[c], HIGH);
  }
  return "";
}

void printScreen(String line1, String line2) {
  Serial.println("\n==================================");
  Serial.println(line1);
  Serial.println(line2);
  Serial.println("==================================\n");
  
  webLine1 = line1;
  webLine2 = line2;

  display.clearDisplay();
  display.setTextColor(WHITE);
  
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(line1);
  display.drawLine(0, 10, 128, 10, WHITE);
  
  if (line2.length() <= 5) {
    display.setTextSize(4);
    
    if (line2.length() <= 2) {
      display.setCursor(40, 24);
    } else {
      display.setCursor(4, 24);
    }
  } else {
    display.setTextSize(2);
    display.setCursor(0, 18);
  }
  
  display.println(line2);
  display.display();
}

void initVirtualBoard() {
  const char backRankW[8] = {'R', 'N', 'B', 'Q', 'K', 'B', 'N', 'R'};
  const char backRankB[8] = {'r', 'n', 'b', 'q', 'k', 'b', 'n', 'r'};
  for (int c = 0; c < 8; c++) {
    board[c][0] = backRankW[c];
    board[c][1] = 'P';
    for (int r = 2; r <= 5; r++) board[c][r] = '.';
    board[c][6] = 'p';
    board[c][7] = backRankB[c];
  }
}

bool isLegalLocalMove(String sSq, String eSq) {
  int c1 = sSq[0] - 'a';
  int r1 = sSq[1] - '1';
  int c2 = eSq[0] - 'a';
  int r2 = eSq[1] - '1';
  char piece  = board[c1][r1];
  char target = board[c2][r2];
  if (piece == '.') return false;
  if (target != '.') {
    if (playingWhite && isUpperCase(target)) return false;
    if (!playingWhite && isLowerCase(target)) return false;
  }
  int dc = c2 - c1;
  int dr = r2 - r1;
  int absDc = abs(dc);
  int absDr = abs(dr);
  switch (toupper(piece)) {
    case 'P': {
      int dir = (isUpperCase(piece)) ? 1 : -1;
      int startRow = (isUpperCase(piece)) ? 1 : 6;
      if (dc == 0 && dr == dir && target == '.') return true; 
      if (dc == 0 && dr == 2 * dir && r1 == startRow && target == '.' && board[c1][r1 + dir] == '.') return true; 
      if (absDc == 1 && dr == dir && (target != '.' || r1 == (isUpperCase(piece) ? 4 : 3))) return true; 
      return false;
    }
    case 'N': return (absDc == 1 && absDr == 2) || (absDc == 2 && absDr == 1);
    case 'B': if (absDc != absDr) return false; break;
    case 'R': if (dc != 0 && dr != 0) return false; break;
    case 'Q': if (dc != 0 && dr != 0 && absDc != absDr) return false; break;
    case 'K':
      if (absDc <= 1 && absDr <= 1) return true;
      if (dr == 0 && absDc == 2 && r1 == (isUpperCase(piece) ? 0 : 7)) return true; 
      return false;
  }
  int stepC = (dc == 0) ? 0 : (dc > 0 ? 1 : -1);
  int stepR = (dr == 0) ? 0 : (dr > 0 ? 1 : -1);
  int currC = c1 + stepC;
  int currR = r1 + stepR;
  while (currC != c2 || currR != r2) {
    if (board[currC][currR] != '.') return false; 
    currC += stepC;
    currR += stepR;
  }
  return true;
}

void applyMoveToBoard(String uciMove) {
  int c1 = uciMove[0] - 'a';
  int r1 = uciMove[1] - '1';
  int c2 = uciMove[2] - 'a';
  int r2 = uciMove[3] - '1';
  char movingPiece = board[c1][r1];
  if (toupper(movingPiece) == 'K' && abs(c2 - c1) == 2) {
    if (c2 == 6) { board[5][r1] = board[7][r1]; board[7][r1] = '.'; } 
    else if (c2 == 2) { board[3][r1] = board[0][r1]; board[0][r1] = '.'; } 
  }
  if (toupper(movingPiece) == 'P' && c1 != c2 && board[c2][r2] == '.') {
    board[c2][r1] = '.';
  }
  board[c2][r2] = movingPiece;
  board[c1][r1] = '.';
}

String getPieceName(char p) {
  switch (toupper(p)) {
    case 'P': return "Pawn";
    case 'R': return "Rook";
    case 'N': return "Knight";
    case 'B': return "Bishop";
    case 'Q': return "Queen";
    case 'K': return "King";
    default:  return "Piece";
  }
}

String extractJsonString(String json, String key) {
  String searchKey = "\"" + key + "\":\"";
  int startIdx = json.indexOf(searchKey);
  if (startIdx == -1) return "";
  startIdx += searchKey.length();
  int endIdx = json.indexOf("\"", startIdx);
  if (endIdx == -1) return "";
  return json.substring(startIdx, endIdx);
}

int countMoves(String movesStr) {
  movesStr.trim();
  if (movesStr.length() == 0) return 0;
  int spaces = 0;
  for (unsigned int i = 0; i < movesStr.length(); i++) {
    if (movesStr[i] == ' ') spaces++;
  }
  return spaces + 1;
}

String getLastMove(String movesStr) {
  movesStr.trim();
  int lastSpace = movesStr.lastIndexOf(' ');
  if (lastSpace == -1) return movesStr;
  return movesStr.substring(lastSpace + 1);
}
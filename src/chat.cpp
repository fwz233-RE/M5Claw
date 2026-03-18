#include "chat.h"
#include <cstring>

// UTF-8 safe line-break: find how many bytes fit within maxW pixels.
static int fitBytes(M5Canvas& canvas, const char* start, int len, int maxW, char* buf, int bufSize) {
    if (len == 0) return 0;

    int tryLen = (len < bufSize - 1) ? len : bufSize - 1;
    memcpy(buf, start, tryLen);
    buf[tryLen] = '\0';
    if (canvas.textWidth(buf) <= maxW) return tryLen;

    int lo = 1, hi = tryLen;
    int best = 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        memcpy(buf, start, mid);
        buf[mid] = '\0';
        if (canvas.textWidth(buf) <= maxW) {
            best = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }

    while (best > 0 && (start[best] & 0xC0) == 0x80) {
        best--;
    }
    if (best == 0) best = 1;

    return best;
}

static int countWrappedLines(M5Canvas& canvas, const char* text, int maxW, char* buf, int bufSize) {
    int len = strlen(text);
    if (len == 0) return 1;
    int pos = 0;
    int lines = 0;
    while (pos < len) {
        int fit = fitBytes(canvas, text + pos, len - pos, maxW, buf, bufSize);
        pos += fit;
        lines++;
    }
    return lines;
}

void Chat::begin(M5Canvas& canvas) {
    if (!initialized) {
        messageCount = 0;
        inputBuffer = "";
        pendingMessage = "";
        waitingForAI = false;
        for (int i = 0; i < MAX_MESSAGES; i++) {
            messages[i].text.reserve(320);
        }
        initialized = true;
    }
    canvas.setFont(&fonts::efontCN_12);
    canvas.setTextSize(1);
    totalContentH = calcTotalHeight(canvas);
    scrollToBottom();
}

void Chat::update(M5Canvas& canvas) {
    canvas.fillScreen(Color::BG_DAY);
    canvas.setFont(&fonts::efontCN_12);
    canvas.setTextSize(1);

    canvas.setTextColor(Color::STATUS_DIM);
    canvas.drawString("[Tab/Ctrl]scroll [Alt]home [Fn]voice", 4, 2);
    canvas.drawFastHLine(0, MSG_AREA_Y - 1, SCREEN_W, Color::GROUND_TOP);

    drawMessages(canvas);
    drawInputBar(canvas);
}

void Chat::handleKey(char key) {
    if (waitingForAI) return;
    if (inputBuffer.length() < 100) {
        inputBuffer += key;
    }
}

void Chat::handleEnter() {
    if (inputBuffer.length() == 0 || waitingForAI) return;

    addMessage(inputBuffer, true);
    pendingMessage = inputBuffer;
    inputBuffer = "";
    waitingForAI = true;
    userScrolled = false;

    addMessage("thinking...", false);
}

void Chat::handleBackspace() {
    if (inputBuffer.length() > 0) {
        inputBuffer.remove(inputBuffer.length() - 1);
    }
}

void Chat::scrollUp() {
    scrollY -= MSG_AREA_H / 2;
    if (scrollY < 0) scrollY = 0;
    userScrolled = true;
}

void Chat::scrollDown() {
    scrollY += MSG_AREA_H / 2;
    int maxScroll = totalContentH - MSG_AREA_H;
    if (maxScroll < 0) maxScroll = 0;
    if (scrollY > maxScroll) scrollY = maxScroll;
    if (scrollY >= maxScroll) userScrolled = false;
}

void Chat::appendAIToken(const char* token) {
    if (messageCount > 0 && !messages[(messageCount - 1) % MAX_MESSAGES].isUser) {
        Message& lastMsg = messages[(messageCount - 1) % MAX_MESSAGES];
        if (lastMsg.text == "thinking...") {
            lastMsg.text = token;
        } else {
            lastMsg.text += token;
        }
        heightsDirty = true;
    }
    if (!userScrolled) scrollToBottom();
}

void Chat::onAIResponseComplete() {
    waitingForAI = false;
    userScrolled = false;
    scrollToBottom();
}


bool Chat::isAtBottom() const {
    int maxScroll = totalContentH - MSG_AREA_H;
    if (maxScroll <= 0) return true;
    return scrollY >= maxScroll;
}

String Chat::takePendingMessage() {
    String msg = pendingMessage;
    pendingMessage = "";
    return msg;
}

void Chat::setInput(const String& text) {
    inputBuffer = text;
}

void Chat::addMessage(const String& text, bool isUser) {
    int idx = messageCount % MAX_MESSAGES;
    messages[idx].isUser = isUser;
    cachedHeights[idx] = 0;
    if (!isUser) {
        messages[idx].text = "";
        messages[idx].text.reserve(320);
        messages[idx].text = text;
    } else {
        messages[idx].text = text;
    }
    messageCount++;
    heightsDirty = true;
    if (!userScrolled) scrollToBottom();
}

int Chat::calcMessageHeight(M5Canvas& canvas, const Message& msg) {
    char buf[64];
    int lines = countWrappedLines(canvas, msg.text.c_str(), MAX_W, buf, sizeof(buf));
    return lines * LINE_H;
}

int Chat::calcTotalHeight(M5Canvas& canvas) {
    int total = min(messageCount, MAX_MESSAGES);
    int startIdx = (messageCount > MAX_MESSAGES) ? (messageCount - MAX_MESSAGES) : 0;
    int h = 0;
    canvas.setTextSize(1);

    int lastIdx = (total > 0) ? (startIdx + total - 1) % MAX_MESSAGES : -1;

    for (int i = 0; i < total; i++) {
        int idx = (startIdx + i) % MAX_MESSAGES;
        if (heightsDirty && idx == lastIdx) {
            cachedHeights[idx] = calcMessageHeight(canvas, messages[idx]);
        } else if (cachedHeights[idx] == 0) {
            cachedHeights[idx] = calcMessageHeight(canvas, messages[idx]);
        }
        h += cachedHeights[idx];
    }

    heightsDirty = false;
    return h;
}

void Chat::scrollToBottom() {
    int maxScroll = totalContentH - MSG_AREA_H;
    if (maxScroll < 0) maxScroll = 0;
    scrollY = maxScroll;
}

void Chat::drawMessages(M5Canvas& canvas) {
    int total = min(messageCount, MAX_MESSAGES);
    int startIdx = (messageCount > MAX_MESSAGES) ? (messageCount - MAX_MESSAGES) : 0;

    canvas.setTextSize(1);
    totalContentH = calcTotalHeight(canvas);

    int maxScroll = totalContentH - MSG_AREA_H;
    if (maxScroll < 0) maxScroll = 0;
    if (scrollY > maxScroll) scrollY = maxScroll;
    if (scrollY < 0) scrollY = 0;
    if (!userScrolled) scrollY = maxScroll;

    int visTop = MSG_AREA_Y;
    int visBot = SCREEN_H - INPUT_BAR_H;

    canvas.setClipRect(0, visTop, SCREEN_W, visBot - visTop);

    char buf[64];
    int y = MSG_AREA_Y + 2 - scrollY;

    for (int i = 0; i < total; i++) {
        int idx = (startIdx + i) % MAX_MESSAGES;
        Message& msg = messages[idx];
        int msgH = cachedHeights[idx];

        if (y + msgH < visTop) {
            y += msgH;
            continue;
        }
        if (y >= visBot) break;

        if (msg.isUser) {
            canvas.setTextColor(Color::CHAT_USER);
            const char* text = msg.text.c_str();
            int len = msg.text.length();
            int pos = 0;
            while (pos < len) {
                int fit = fitBytes(canvas, text + pos, len - pos, MAX_W, buf, sizeof(buf));
                if (y >= visTop - LINE_H && y < visBot) {
                    memcpy(buf, text + pos, fit);
                    buf[fit] = '\0';
                    int tw = canvas.textWidth(buf);
                    int tx = SCREEN_W - tw - 6;
                    if (tx < 4) tx = 4;
                    canvas.fillRoundRect(tx - 2, y - 1, min(tw + 4, SCREEN_W - 4), LINE_H, 2, Color::INPUT_BG);
                    canvas.drawString(buf, tx, y);
                }
                pos += fit;
                y += LINE_H;
            }
            if (len == 0) y += LINE_H;
        } else {
            canvas.setTextColor(Color::CHAT_AI);
            const char* text = msg.text.c_str();
            int len = msg.text.length();
            int pos = 0;

            while (pos < len) {
                int fit = fitBytes(canvas, text + pos, len - pos, MAX_W, buf, sizeof(buf));

                if (y >= visTop - LINE_H && y < visBot) {
                    memcpy(buf, text + pos, fit);
                    buf[fit] = '\0';
                    canvas.drawString(buf, 6, y);
                }

                pos += fit;
                y += LINE_H;
            }
            if (len == 0) y += LINE_H;
        }
    }

    canvas.clearClipRect();

    if (totalContentH > MSG_AREA_H && maxScroll > 0) {
        int barH = max(8, MSG_AREA_H * MSG_AREA_H / totalContentH);
        int barY = MSG_AREA_Y + (scrollY * (MSG_AREA_H - barH)) / maxScroll;
        canvas.fillRect(SCREEN_W - 2, barY, 2, barH, Color::STATUS_DIM);
    }
}

void Chat::drawInputBar(M5Canvas& canvas) {
    int barY = SCREEN_H - INPUT_BAR_H;
    canvas.fillRect(0, barY, SCREEN_W, INPUT_BAR_H, Color::INPUT_BG);
    canvas.drawFastHLine(0, barY, SCREEN_W, Color::GROUND_TOP);

    canvas.setTextColor(Color::WHITE);
    canvas.setTextSize(1);

    if (waitingForAI) {
        canvas.setTextColor(Color::STATUS_DIM);
        canvas.drawString("waiting...", 4, barY + 4);
    } else {
        char display[128];
        snprintf(display, sizeof(display), "> %s_", inputBuffer.c_str());
        const char* p = display;
        while (canvas.textWidth(p) > SCREEN_W - 8 && strlen(p) > 4) {
            p += 1;
            while ((*p & 0xC0) == 0x80) p++;
        }
        if (p != display) {
            char truncated[128];
            snprintf(truncated, sizeof(truncated), "> %s", p);
            canvas.drawString(truncated, 4, barY + 4);
        } else {
            canvas.drawString(display, 4, barY + 4);
        }
    }
}

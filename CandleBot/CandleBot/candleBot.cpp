#include <iostream>
#include <fstream>
#include <vector>
#include <thread>
#include <chrono>
#include <string>
#include <curl/curl.h>
using namespace std;

size_t WriteCallback(void* contents, size_t size, size_t nmemb, string* s) {
    size_t newLength = size * nmemb;
    s->append((char*)contents, newLength);
    return newLength;
}

struct Candle { double open; double close; };

bool isMorningStar(const vector<Candle>& candles) {
    if (candles.size() < 3) return false;
    int n = candles.size();
    double drop1 = candles[n - 3].open - candles[n - 3].close;
    bool longBearish = drop1 > 0 && drop1 > 2.0;
    double body2 = abs(candles[n - 2].close - candles[n - 2].open);
    bool shortBody = body2 < 1.0;
    double rise3 = candles[n - 1].close - candles[n - 1].open;
    double midpoint1 = candles[n - 3].open - (drop1 / 2);
    bool longBullish = rise3 > 0 && rise3 > 2.0 && candles[n - 1].close > midpoint1;
    return longBearish && shortBody && longBullish;
}

bool isEveningStar(const vector<Candle>& candles) {
    if (candles.size() < 3) return false;
    int n = candles.size();
    double rise1 = candles[n - 3].close - candles[n - 3].open;
    bool longBullish = rise1 > 0 && rise1 > 2.0;
    double body2 = abs(candles[n - 2].close - candles[n - 2].open);
    bool shortBody = body2 < 1.0;
    double drop3 = candles[n - 1].open - candles[n - 1].close;
    double midpoint1 = candles[n - 3].open + (rise1 / 2);
    bool longBearish = drop3 > 0 && drop3 > 2.0 && candles[n - 1].close < midpoint1;
    return longBullish && shortBody && longBearish;
}

Candle fetchLatestCandle(const string& apiKey) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        cout << "CURL init failed!" << endl;
        return { 0.0, 0.0 };
    }
    string url = "https://www.alphavantage.co/query?function=TIME_SERIES_INTRADAY&symbol=SPY&interval=1min&apikey=" + apiKey;
    string response;
    cout << "Fetching: " << url << endl;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        cout << "CURL failed: " << curl_easy_strerror(res) << endl;
        curl_easy_cleanup(curl);
        return { 0.0, 0.0 };
    }
    curl_easy_cleanup(curl);

    // Find the latest timestamp block
    size_t timeSeriesPos = response.find("\"Time Series (1min)\": {");
    if (timeSeriesPos == string::npos) {
        cout << "No Time Series data: " << response << endl;
        return { 0.0, 0.0 };
    }
    size_t firstQuote = response.find("\"", timeSeriesPos + 22);
    if (firstQuote == string::npos) {
        cout << "No timestamp found: " << response << endl;
        return { 0.0, 0.0 };
    }
    size_t secondQuote = response.find("\"", firstQuote + 1);
    if (secondQuote == string::npos) {
        cout << "Invalid timestamp format: " << response << endl;
        return { 0.0, 0.0 };
    }
    string latestTime = response.substr(firstQuote + 1, secondQuote - firstQuote - 1);
    cout << "Latest timestamp: " << latestTime << endl;

    // Get the block after the timestamp
    size_t blockStart = response.find("{", secondQuote + 1);
    if (blockStart == string::npos) {
        cout << "No opening brace after timestamp: " << response << endl;
        return { 0.0, 0.0 };
    }
    blockStart += 1; // Skip "{"
    size_t blockEnd = response.find("}", blockStart);
    if (blockEnd == string::npos) {
        cout << "No closing brace in block: " << response << endl;
        return { 0.0, 0.0 };
    }
    string block = response.substr(blockStart, blockEnd - blockStart);
    cout << "Block: " << block << endl;

    // Parse open
    size_t pos = block.find("\"1. open\": \"");
    if (pos == string::npos) {
        cout << "No open price in block: " << block << endl;
        return { 0.0, 0.0 };
    }
    size_t start = pos + 11; // After "1. open": "
    while (start < block.length() && (block[start] == ' ' || block[start] == '\n' || block[start] == '\t')) {
        start++; // Skip whitespace
    }
    if (start >= block.length() || block[start] != '"') {
        cout << "Invalid open quote format in block: " << block << endl;
        return { 0.0, 0.0 };
    }
    start++; // Skip opening quote
    size_t end = block.find("\"", start);
    if (end == string::npos || start >= end) {
        cout << "Invalid open end quote in block: " << block << endl;
        return { 0.0, 0.0 };
    }
    string openStr = block.substr(start, end - start);
    double open;
    try {
        open = stod(openStr);
    }
    catch (const std::invalid_argument& e) {
        cout << "Invalid open value '" << openStr << "': " << e.what() << endl;
        return { 0.0, 0.0 };
    }

    // Parse close
    pos = block.find("\"4. close\": \"");
    if (pos == string::npos) {
        cout << "No close price in block: " << block << endl;
        return { 0.0, 0.0 };
    }
    start = pos + 12; // After "4. close": "
    while (start < block.length() && (block[start] == ' ' || block[start] == '\n' || block[start] == '\t')) {
        start++; // Skip whitespace
    }
    if (start >= block.length() || block[start] != '"') {
        cout << "Invalid close quote format in block: " << block << endl;
        return { 0.0, 0.0 };
    }
    start++; // Skip opening quote
    end = block.find("\"", start);
    if (end == string::npos || start >= end) {
        cout << "Invalid close end quote in block: " << block << endl;
        return { 0.0, 0.0 };
    }
    string closeStr = block.substr(start, end - start);
    double close;
    try {
        close = stod(closeStr);
    }
    catch (const std::invalid_argument& e) {
        cout << "Invalid close value '" << closeStr << "': " << e.what() << endl;
        return { 0.0, 0.0 };
    }

    cout << "Parsed - Open: " << open << ", Close: " << close << endl;
    return { open, close };
}

int main() {
    double cash = 1000.0;
    int shares = 0;
    int shortShares = 0;
    double longEntryPrice = 0.0;
    double shortEntryPrice = 0.0;
    vector<Candle> candles;
    string apiKey;
    ifstream configFile("config.txt");
    if (configFile.is_open()) {
        getline(configFile, apiKey);
        configFile.close();
    }
    else {
        cout << "No config.txt!" << endl;
        return 1;
    }
    const double profitTarget = 0.10;
    ofstream tradeLog("trades.txt", ios::app);
    if (!tradeLog.is_open()) {
        cout << "Can’t open trades.txt!" << endl;
        return 1;
    }
    cout << "Welcome to CandleBot! Monitoring SPY live (13s intervals)..." << endl;
    while (true) {
        Candle newCandle = fetchLatestCandle(apiKey);
        if (newCandle.open == 0.0 && newCandle.close == 0.0) {
            cout << "Fetch error! Retrying in 13s..." << endl;
        }
        else {
            candles.push_back(newCandle);
            cout << "Latest SPY - Open: " << newCandle.open << ", Close: " << newCandle.close << endl;
        }
        if (candles.size() >= 3) {
            double currentPrice = candles.back().close;
            if (shares > 0 && currentPrice >= longEntryPrice * (1 + profitTarget)) {
                cout << "10% profit! Selling at $" << currentPrice << endl;
                double profit = currentPrice - longEntryPrice;
                cash += currentPrice;
                shares--;
                tradeLog << "Sold 1 SPY at $" << currentPrice << " (Bought $" << longEntryPrice
                    << ", Profit: $" << profit << ", " << (profit / longEntryPrice * 100) << "%)" << endl;
                longEntryPrice = 0.0;
            }
            if (shortShares > 0 && currentPrice <= shortEntryPrice * (1 - profitTarget)) {
                cout << "10% profit! Covering short at $" << currentPrice << endl;
                double profit = shortEntryPrice - currentPrice;
                cash -= currentPrice;
                shortShares--;
                tradeLog << "Covered 1 SPY short at $" << currentPrice << " (Shorted $" << shortEntryPrice
                    << ", Profit: $" << profit << ", " << (profit / shortEntryPrice * 100) << "%)" << endl;
                shortEntryPrice = 0.0;
            }
            if (isMorningStar(candles)) {
                cout << "Morning Star detected!" << endl;
                if (cash >= currentPrice) {
                    cout << "Buying at $" << currentPrice << endl;
                    shares++;
                    cash -= currentPrice;
                    longEntryPrice = currentPrice;
                    tradeLog << "Bought 1 SPY at $" << currentPrice << endl;
                }
                else {
                    cout << "Not enough cash!" << endl;
                }
                if (shortShares > 0) {
                    cout << "Covering short at $" << currentPrice << endl;
                    double profit = shortEntryPrice - currentPrice;
                    cash -= currentPrice;
                    shortShares--;
                    tradeLog << "Covered 1 SPY short at $" << currentPrice << " (Shorted $" << shortEntryPrice
                        << ", Profit: $" << profit << ", " << (profit / shortEntryPrice * 100) << "%)" << endl;
                    shortEntryPrice = 0.0;
                }
            }
            else if (isEveningStar(candles)) {
                cout << "Evening Star detected!" << endl;
                if (shares > 0) {
                    cout << "Selling at $" << currentPrice << endl;
                    double profit = currentPrice - longEntryPrice;
                    cash += currentPrice;
                    shares--;
                    tradeLog << "Sold 1 SPY at $" << currentPrice << " (Bought $" << longEntryPrice
                        << ", Profit: $" << profit << ", " << (profit / longEntryPrice * 100) << "%)" << endl;
                    longEntryPrice = 0.0;
                }
                if (shortShares == 0) {
                    cout << "Shorting at $" << currentPrice << endl;
                    shortShares++;
                    cash += currentPrice;
                    shortEntryPrice = currentPrice;
                    tradeLog << "Shorted 1 SPY at $" << currentPrice << endl;
                }
                else {
                    cout << "Already short!" << endl;
                }
            }
            else {
                cout << "No pattern." << endl;
            }
            cout << "Cash: $" << cash << ", Long: " << shares << ", Short: " << shortShares << endl;
        }
        this_thread::sleep_for(chrono::seconds(13));
    }
    tradeLog.close();
    return 0;
}
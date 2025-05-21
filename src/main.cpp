#include <iostream>
#include <chrono>
#include <vector>

#include "WindowManager.h"

using namespace std;

namespace Functions {
    // Converts an integer into a vector of its digits
    vector<int> intToVector(int num) {
        vector<int> resultArray(2, 0);
        while (true) {
            resultArray.insert(resultArray.begin(), num % 10);
            num /= 10;
            if (num == 0)
                return resultArray;
        }
    }

    // Reads an integer input from the user
    int intUserInput() {
        int _temp;
        cin >> _temp;
        return _temp;
    }
}

using namespace Functions;

int main() {
    using clock = chrono::steady_clock;
    auto lastTime = clock::now();
    float deltaTime = 0.0f;

    WindowManager window;
    window.MakeNewWindow();
    cout << "Made a new window.\n" << endl;

    // Main application loop
    while (!window.isClosed()) {
        auto now = clock::now();
        chrono::duration<float> elapsed = now - lastTime;
        deltaTime = elapsed.count();
        lastTime = now;

        window.Update(deltaTime);
    }

    glfwTerminate();
    return 0;
}
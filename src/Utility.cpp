#include "Utility.h"
#include <iostream>

vector<int> intToVector(int num) {
    vector resultArray(2, 0);
    while (true) {
        resultArray.insert(resultArray.begin(), num % 10);
        num /= 10;
        if (num == 0)
            return resultArray;
    }
}

// Reads an integer input from the user
int intUserInput()
{
    int _temp;
    cin >> _temp;
    return _temp;
}
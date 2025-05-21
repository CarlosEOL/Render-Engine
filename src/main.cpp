
#include <iostream>
#include <chrono>   //For duration - From ChatGPT
#include <vector>

#include "WindowManager.h"

const unsigned int WIDTH = 1000, HEIGHT = 1000; //Window Size

using namespace std;
namespace Functions
{
    //Helper Codes//
    
    //Function Def
    vector<int> intToVector(int num) //See Reference: https://stackoverflow.com/questions/1860983/convert-integer-to-array
    {
        vector <int> resultArray = vector<int>(2, 0);
        while (true)
        {
            resultArray.insert(resultArray.begin(), num % 10);
            num /= 10;
            if (num == 0)
                return resultArray;
        }
    }

    int intUserInput()
    {
        int _temp;
        cin >> _temp;

        return _temp;
    }

}

// CONTEXT: THIS IS THE MAIN FILE //

using namespace Functions;
int main()
{
    using clock = std::chrono::steady_clock;
    auto lastTime = clock::now();
    
    WindowManager window;
    window.MakeNewWindow(WIDTH, HEIGHT);
    cout<<"Made a new window.\n" << endl;
    
    //Enter Game State, handles user input// window.isClosed()
    while (!window.isClosed())
    {
        auto now = clock::now();
        chrono::duration<float> elapsed = now - lastTime;

        float deltaTime = elapsed.count();
        
        lastTime = now;
        
        window.Update();
/*
        //Take in User Input in Console//
        cout << "\n\nEnter 2 numbers indicating rows and columns, ex - (y,x): \n";
        vector<int> array = intToVector(intUserInput());

        ///cout << _temp << " is what you inputed, covert to " << array[0] << "," << array[1]; //DEBUG CODE
        grid.DrawInputedTiles(array); //Temp Initialized Coord
        this_thread::sleep_for(chrono::seconds(1));

        if (!grid.CheckTiles(array[0], array[1])) //check if the tile is false
            Lifes--;

        //DEBUG CODE//
        //cout << grid.CheckTiles(array[0], array[1]);

 * Old Code for Text-based game, Use GLFW for UXR
 
        switch (!Lifes) //If the game continues
        {
        case true: //When Player has no more life, increase difficulty.

            Level++;
            Lifes = 3;

            if (Level & 2)
            {
                
                grid.IncreaseSize();
                grid.GenerateSqrTiles();
                grid.DrawStringTiles();
                grid.HideTiles();
            }

            break;

        case false: //Game Logic when player has life

            //grid.DrawStringTiles();
            //this_thread::sleep_for(chrono::seconds(1));
            //Lifes--;
            break;
        }

        this_thread::sleep_for(chrono::seconds(1));
*/
    }
    
    glfwTerminate();
    
    return 0;
}
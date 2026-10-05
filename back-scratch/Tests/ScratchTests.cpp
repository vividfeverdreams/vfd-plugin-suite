#include "PatternTests.h"
#include "PatternEditingTests.h"
#include "DrawToolsTests.h"
#include "MotionProfileTests.h"
#include "LiveEngineTests.h"
#include "EngineContinuityTests.h"
#include <iostream>
int main()
{
    try {
        backScratch::runPatternTests();
        backScratch::runPatternEditingTests();
        backScratch::runDrawToolsTests();
        backScratch::runMotionProfileTests();
        runLiveEngineTests();
        runEngineContinuityTests();
        std::cout << "BACK-SCRATCH live engine and pattern tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}

//
//  Scanner.h
//  moka
//
//  Created by Ce Zhang on 1/14/15.
//  Copyright (c) 2015 Hazy Research. All rights reserved.
//

#include "Cube.h"
#include "Report.h"

#ifndef moka_Scanner_h
#define moka_Scanner_h

enum NonLinearFunction{
    FUNC_NOFUNC = 0,
    FUNC_TANH = 1
};

/**
 * A scanner is simple -- for each element in a Cube, apply a function, and update the element.
 **/
template
<typename DataType, LayoutType Layout, NonLinearFunction SCANNER>
class Scanner{
public:
    
    typedef Cube<DataType, Layout> CubeType;
    
    Report report_constructor; /*< Performance reporter for constructor function. */
    Report report_last_apply; /*< Performance reporter for the last run of transfer() function. */
    Report report_history; /*< Performance reporter for all transfer() functions aggregated. */
    
    Scanner(const CubeType * const p_cube){
        std::cerr << "ERROR: Using a scanner with unsupported Layout or DataType." << std::endl;
        assert(false);
    }
    
    void apply(CubeType * const p_cube){
        std::cerr << "ERROR: Using a scanner with unsupported Layout or DataType." << std::endl;
        assert(false);
    }
    
};

/******
 * Specializations
 */
template
<typename DataType, LayoutType Layout>
class Scanner<DataType, Layout, FUNC_TANH>{
public:
    
    typedef Cube<DataType, Layout> CubeType;
    
    Report report_constructor; /*< Performance reporter for constructor function. */
    Report report_last_apply; /*< Performance reporter for the last run of transfer() function. */
    Report report_history; /*< Performance reporter for all transfer() functions aggregated. */
    
    Scanner(const CubeType * const p_cube);
    
    void apply(CubeType * const p_cube);
    
};

template
<typename DataType, LayoutType Layout>
class Scanner<DataType, Layout, FUNC_NOFUNC>{
public:
    
    typedef Cube<DataType, Layout> CubeType;
    
    Report report_constructor; /*< Performance reporter for constructor function. */
    Report report_last_apply; /*< Performance reporter for the last run of transfer() function. */
    Report report_history; /*< Performance reporter for all transfer() functions aggregated. */

    Scanner(const CubeType * const p_cube){
        report_constructor.reset();
        report_last_apply.reset();
        report_history.reset();
        
        report_constructor.end(0, 0, 0);
    }
    
    void apply(CubeType * const p_cube){}
    
};

#include "Scanner_impl.hxx"

#endif
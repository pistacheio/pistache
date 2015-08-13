/* os.h
   Mathieu Stefani, 13 August 2015
   
   Operating system specific functions
*/

#pragma once

typedef int Fd;

int hardware_concurrency();
bool make_non_blocking(int fd);


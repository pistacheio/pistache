/*
 * From https://stackoverflow.com/questions/225432/export-all-symbols-when-creating-a-dll
 * Public domain
 */

#define UNICODE
#define _UNICODE

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <set>

#include <locale>

#include <algorithm>

#include <windows.h>
#include <errhandlingapi.h> // getLastError
#include <stringapiset.h> // MultiByteToWideChar

typedef std::set<std::wstring> SymbolMap;

void PrintHelpAndExit(int code)
{
    std::cout << "dump2def - create a module definitions file from a dumpbin file" << std::endl;
    std::cout << "           Written and placed in public domain by Jeffrey Walton" << std::endl;
    std::cout << "           Updated by DMG" << std::endl;
    std::cout << std::endl;

    std::cout << "Usage: " << std::endl;

    std::cout << "  dump2def <infile>" << std::endl;
    std::cout << "    - Create a def file from <infile> and write it to a file with" << std::endl;
    std::cout << "      the same name as <infile> but using the .def extension" << std::endl;

    std::cout << "  dump2def <infile> <outfile>" << std::endl;
    std::cout << "    - Create a def file from <infile> and write it to <outfile>" << std::endl;

    std::exit(code);
}

int main(int argc, char* argv[])
{
    // ******************** Handle Options ******************** //

    // Convenience item
    std::vector<std::string> opts;
    for (size_t i=0; i<argc; ++i)
        opts.push_back(argv[i]);

    // Look for help
    std::string opt = opts.size() < 3 ? "" : opts[1].substr(0,2);
    if (opt == "/h" || opt == "-h" || opt == "/?" || opt == "-?")
        PrintHelpAndExit(0);

    // Add <outfile> as needed
    if (opts.size() == 2)
    {
        std::string outfile = opts[1];
        std::string::size_type pos = outfile.length() < 5 ? std::string::npos : outfile.length() - 5;
        if (pos == std::string::npos || outfile.substr(pos) != ".dump")
            PrintHelpAndExit(1);

        outfile.replace(pos, 5, ".def");
        opts.push_back(outfile);
    }

    // Check or exit
    if (opts.size() != 3)
        PrintHelpAndExit(1);

    // ******************** Read MAP file ******************** //

    SymbolMap symbols;

    unsigned int num_sym_found = 0;

    try
    {
        std::cout << "Accessing file: " << opts[1] << std::endl;

        std::wifstream infile(opts[1].c_str());
        std::string::size_type fnd_pos;
        std::wstring line;
        
        unsigned int num_line = 0;
        
        while (std::getline(infile, line))
        {
            num_line++;

            fnd_pos = line.find(L"public symbols");
            if (fnd_pos == std::string::npos) { continue; }
            
            std::cout << "Public symbols line found" << std::endl;

            // Eat the whitespace after the table heading
            infile >> std::ws;
            break;
        }

        while (std::getline(infile, line))
        {
            // End of table
            if (line.empty())
            {
                if (num_sym_found)
                    break;
                else
                    continue;
            }

            num_sym_found++;

            std::wistringstream iss(line);
            std::wstring address, symbol;
            iss >> address >> symbol;

            symbols.insert(symbol);
        }
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Unexpected exception:" << std::endl;
        std::cerr << ex.what() << std::endl;
        std::cerr << std::endl;

        PrintHelpAndExit(1);
    }

    std::cout << "Number of symbols found " << num_sym_found <<
        ", vector size: " << symbols.size() << std::endl;

    // ******************** Write DEF file ******************** //

    try
    {
        // If we wanted to make the .def file a wchar_t file, we'd use
        // std::wofstream here; prefix static strings to be output with "L";
        // output name_ws rather than name; and output each symbol without
        // converting to symbol_ns.
        
        std::wofstream outfile(opts[2].c_str());

        // Library name
        std::string name = opts[2];
        std::string::size_type pos = name.find_last_of(".");

        if (pos != std::string::npos)
            name.erase(pos);

        #define name_wc_buf_size_in_wc 4096
        wchar_t name_wcs[name_wc_buf_size_in_wc+16];
        
        int mbtwc_res = MultiByteToWideChar(CP_UTF8,
                                         MB_ERR_INVALID_CHARS, // fail on inval
                                         name.c_str(),
                                         -1, // name len => null-terminated
                                         &(name_wcs[0]),
                                         name_wc_buf_size_in_wc);
        if (mbtwc_res <= 0)
        {
            std::cerr << "Error: MultiByteToWideChar failure converting "
                      << name << std::endl;
            auto last_err = GetLastError();
            
            const char * errmsg = "unexpected error code";
            switch(last_err)
            {
            case ERROR_INSUFFICIENT_BUFFER:
                errmsg = "insufficient buffer";
                break;

            case ERROR_INVALID_FLAGS:
                errmsg = "invalid flags";
                break;

            case ERROR_INVALID_PARAMETER:
                errmsg = "invalid parameter";
                break;

            case ERROR_NO_UNICODE_TRANSLATION:
                errmsg = "no unicode translation";
                break;

            default:
                break;
            }

            std::cerr << "       error code " <<  last_err <<
                " (" << errmsg << ")" << std::endl;

            PrintHelpAndExit(1);
        }
        
        std::wstring name_ws(&(name_wcs[0]));

        outfile << L"LIBRARY " << name_ws << std::endl;
        outfile << L"EXPORTS" << std::endl;
        outfile << std::endl;

        outfile << L"\t;; " << symbols.size() << L" symbols" << std::endl;

        // Symbols from our object files
        SymbolMap::const_iterator it = symbols.begin();
        for ( ; it != symbols.end(); ++it)
        {
            outfile << L"\t" << (*it) << std::endl;
        }
        
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Unexpected exception:" << std::endl;
        std::cerr << ex.what() << std::endl;
        std::cerr << std::endl;

        PrintHelpAndExit(1);
    }   

    return 0;
}

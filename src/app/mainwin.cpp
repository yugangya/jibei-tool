/***************************************************************************
    mainwin.cpp
    ---------------------
    begin                : February 2017
    copyright            : (C) 2017 by Juergen E. Fischer
    email                : jef at norbit dot de
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/
#include <fstream>
#include <io.h>
#include <iostream>
#include <list>
#include <memory>
#include <sstream>
#include <vector>
#include <windows.h>

void showError( std::string message, std::string title )
{
  std::string newmessage = "Oops, looks like an error loading QGIS \n\n Details: \n\n" + message;
  MessageBox( NULL, newmessage.c_str(), title.c_str(), MB_ICONERROR | MB_OK );
  std::cerr << message << std::endl;
}

std::string moduleExeBaseName( void )
{
  DWORD l = MAX_PATH;
  std::unique_ptr<char> filepath;
  for ( ;; )
  {
    filepath.reset( new char[l] );
    if ( GetModuleFileName( nullptr, filepath.get(), l ) < l )
      break;

    l += MAX_PATH;
  }

  std::string basename( filepath.get() );
  return basename;
}

void setupPortableEnvironment( const std::string & /*exePath */ )
{
  // The deployment package may live below a Chinese or otherwise non-ACP
  // path.  The old ANSI GetModuleFileName/_putenv path silently replaced
  // those characters with '?', producing invalid PYTHONHOME/PYTHONPATH and
  // disabling Python only on the target machine.  Keep all portable path
  // calculations in UTF-16 until they reach the Windows environment.
  DWORD capacity = MAX_PATH;
  std::wstring exePath;
  for ( ;; )
  {
    std::vector<wchar_t> buffer( capacity );
    const DWORD length = GetModuleFileNameW( nullptr, buffer.data(), capacity );
    if ( length == 0 )
      return;
    if ( length < capacity - 1 )
    {
      exePath.assign( buffer.data(), length );
      break;
    }
    capacity *= 2;
    if ( capacity > 32768 )
      return;
  }

  const std::wstring::size_type slash = exePath.find_last_of( L"\\/" );
  if ( slash == std::wstring::npos )
    return;

  const std::wstring binDir = exePath.substr( 0, slash );
  const std::wstring::size_type prefixSlash = binDir.find_last_of( L"\\/" );
  if ( prefixSlash == std::wstring::npos )
    return;
  const std::wstring prefixDir = binDir.substr( 0, prefixSlash );
  const std::wstring::size_type packageSlash = prefixDir.find_last_of( L"\\/" );
  if ( packageSlash == std::wstring::npos )
    return;
  const std::wstring packageDir = prefixDir.substr( 0, packageSlash );
  const std::wstring osgeoRoot = packageDir + L"\\osgeo4w";
  const std::wstring pythonDir = osgeoRoot + L"\\apps\\Python312";
  const std::wstring qtDir = osgeoRoot + L"\\apps\\Qt6";

  // Only activate this fallback for the portable Jianghe package layout.
  if ( _waccess( ( prefixDir + L"\\python" ).c_str(), 0 ) != 0 ||
       _waccess( qtDir.c_str(), 0 ) != 0 ||
       _waccess( pythonDir.c_str(), 0 ) != 0 )
    return;

  const std::wstring gdalRoot = osgeoRoot + L"\\apps\\gdal-dev";
  const std::wstring pdalRoot = osgeoRoot + L"\\apps\\pdal-dev";
  const std::wstring msysRoot = osgeoRoot + L"\\apps\\msys";
  const std::wstring pluginPath = binDir + L"\\qtplugins";
  const std::wstring qtPluginPath = qtDir + L"\\plugins";
  const std::wstring onnxRoot = packageDir + L"\\onnxruntime";
  const wchar_t *systemRootEnv = _wgetenv( L"SystemRoot" );
  const std::wstring systemRoot = systemRootEnv ? systemRootEnv : L"C:\\Windows";
  const std::wstring path = binDir + L";" + prefixDir + L";" +
                            osgeoRoot + L"\\bin;" + qtDir + L"\\bin;" +
                            gdalRoot + L"\\bin;" + pdalRoot + L"\\bin;" +
                            msysRoot + L"\\bin;" + pythonDir + L";" +
                            pythonDir + L"\\Scripts;" + pythonDir + L"\\DLLs;" +
                            onnxRoot + L"\\lib;" + systemRoot + L"\\System32;" +
                            systemRoot;

  _wputenv( ( L"QGIS_PREFIX_PATH=" + prefixDir ).c_str() );
  _wputenv( ( L"QGIS_PLUGINPATH=" + prefixDir + L"\\plugins" ).c_str() );
  _wputenv( ( L"QT_PLUGIN_PATH=" + qtPluginPath + L";" + pluginPath ).c_str() );
  _wputenv( ( L"QCA_PLUGIN_PATH=" + qtPluginPath + L";" + pluginPath ).c_str() );
  _wputenv( ( L"QT_QPA_PLATFORM_PLUGIN_PATH=" + qtPluginPath + L"\\platforms" ).c_str() );
  _wputenv( ( L"PYTHONHOME=" + pythonDir ).c_str() );
  _wputenv( ( L"PYTHONPATH=" + prefixDir + L"\\python;" + pythonDir + L"\\Lib;" +
              pythonDir + L"\\DLLs;" + pythonDir + L"\\Lib\\site-packages;" +
              packageDir + L"\\python-support" ).c_str() );
  _wputenv( L"PYTHONNOUSERSITE=1" );
  _wputenv( ( L"GDAL_DATA=" + osgeoRoot + L"\\share\\gdal" ).c_str() );
  _wputenv( ( L"PROJ_DATA=" + osgeoRoot + L"\\share\\proj" ).c_str() );
  _wputenv( ( L"PROJ_LIB=" + osgeoRoot + L"\\share\\proj" ).c_str() );
  _wputenv( ( L"PATH=" + path ).c_str() );
}

int CALLBACK WinMain( HINSTANCE /*hInstance*/, HINSTANCE /*hPrevInstance*/, LPSTR /*lpCmdLine*/, int /*nCmdShow*/ )
{
  std::string exename( moduleExeBaseName() );
  std::string basename( exename.substr( 0, exename.size() - 4 ) );

  // Directly double-clicked portable builds need the same environment as the launcher script.
  setupPortableEnvironment( exename );

  if ( getenv( "OSGEO4W_ROOT" ) && __argc == 2 && strcmp( __argv[1], "--postinstall" ) == 0 )
  {
    std::string envfile( basename + ".env" );

    // write or update environment file
    if ( _access( envfile.c_str(), 0 ) < 0 || _access( envfile.c_str(), 2 ) == 0 )
    {
      std::list<std::string> vars;

      try
      {
        std::ifstream varfile;
        varfile.open( basename + ".vars" );

        std::string var;
        while ( std::getline( varfile, var ) )
        {
          vars.push_back( var );
        }

        varfile.close();
      }
      catch ( std::ifstream::failure &e )
      {
        std::string message = "Could not read environment variable list " + basename + ".vars" + " [" + e.what() + "]";
        showError( message, "Error loading QGIS" );
        return EXIT_FAILURE;
      }

      try
      {
        std::ofstream file;
        file.open( envfile, std::ifstream::out );

        for ( std::list<std::string>::const_iterator it = vars.begin(); it != vars.end(); ++it )
        {
          if ( getenv( it->c_str() ) )
            file << *it << "=" << getenv( it->c_str() ) << std::endl;
        }
      }
      catch ( std::ifstream::failure &e )
      {
        std::string message = "Could not write environment file " + basename + ".env" + " [" + e.what() + "]";
        showError( message, "Error loading QGIS" );
        return EXIT_FAILURE;
      }
    }

    return EXIT_SUCCESS;
  }

  try
  {
    std::ifstream file;
    file.open( basename + ".env" );

    std::string var;
    while ( std::getline( file, var ) )
    {
      if ( _putenv( var.c_str() ) < 0 )
      {
        std::string message = "Could not set environment variable:" + var;
        showError( message, "Error loading QGIS" );
        return EXIT_FAILURE;
      }
    }
  }
  catch ( std::ifstream::failure &e )
  {
    std::string message = "Could not read environment file " + basename + ".env" + " [" + e.what() + "]";
    showError( message, "Error loading QGIS" );
    return EXIT_FAILURE;
  }

#ifndef _MSC_VER // MinGW
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
  HINSTANCE hKernelDLL = LoadLibrary( "kernel32.dll" );
  BOOL ( *SetDefaultDllDirectories )( DWORD ) = hKernelDLL ? reinterpret_cast<BOOL ( * )( DWORD )>( GetProcAddress( hKernelDLL, "SetDefaultDllDirectories" ) ) : 0;
  DLL_DIRECTORY_COOKIE ( *AddDllDirectory )( PCWSTR ) = hKernelDLL ? reinterpret_cast<DLL_DIRECTORY_COOKIE ( * )( PCWSTR )>( GetProcAddress( hKernelDLL, "AddDllDirectory" ) ) : 0;
#ifndef _MSC_VER // MinGW
#pragma GCC diagnostic pop
#endif

  if ( SetDefaultDllDirectories && AddDllDirectory )
  {
    SetDefaultDllDirectories( LOAD_LIBRARY_SEARCH_DEFAULT_DIRS );

    wchar_t windir[MAX_PATH];
    GetWindowsDirectoryW( windir, MAX_PATH );
    wchar_t systemdir[MAX_PATH];
    GetSystemDirectoryW( systemdir, MAX_PATH );

    wchar_t *path = wcsdup( _wgetenv( L"PATH" ) );

#ifdef _UCRT
    for ( wchar_t *p = wcstok( path, L";", nullptr ); p; p = wcstok( NULL, L";", nullptr ) )
#else
    for ( wchar_t *p = wcstok( path, L";" ); p; p = wcstok( NULL, L";" ) )
#endif
    {
      if ( wcsicmp( p, windir ) == 0 )
        continue;
      if ( wcsicmp( p, systemdir ) == 0 )
        continue;
      AddDllDirectory( p );
    }

    free( path );
  }


#ifdef _MSC_VER
  HINSTANCE hGetProcIDDLL = LoadLibrary( "qgis_app.dll" );
#else
  // MinGW
  HINSTANCE hGetProcIDDLL = LoadLibrary( "libqgis_app.dll" );
#endif

  if ( !hGetProcIDDLL )
  {
    DWORD error = GetLastError();
    LPTSTR errorText = NULL;

    FormatMessage( FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, error, MAKELANGID( LANG_NEUTRAL, SUBLANG_DEFAULT ), ( LPTSTR ) &errorText, 0, NULL );

    std::string message = "Could not load qgis_app.dll \n Windows Error: " + std::string( errorText ) + "\n Help: \n\n Check " + basename + ".env for correct environment paths";
    showError( message, "Error loading QGIS" );

    LocalFree( errorText );
    errorText = NULL;
    return EXIT_FAILURE;
  }

#ifndef _MSC_VER // MinGW
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
  int ( *realmain )( int, char *[] ) = ( int ( * )( int, char *[] ) ) GetProcAddress( hGetProcIDDLL, "main" );
#ifndef _MSC_VER // MinGW
#pragma GCC diagnostic pop
#endif

  if ( !realmain )
  {
    showError( "Could not locate main function in qgis_app.dll", "Error loading QGIS" );
    return EXIT_FAILURE;
  }

  return realmain( __argc, __argv );
}

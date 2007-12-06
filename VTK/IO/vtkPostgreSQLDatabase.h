/*=========================================================================

  Program:   Visualization Toolkit
  Module:    $RCSfile$

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
/*----------------------------------------------------------------------------
 Copyright (c) Sandia Corporation
 See Copyright.txt or http://www.paraview.org/HTML/Copyright.html for details.
----------------------------------------------------------------------------*/
// .NAME vtkPostgreSQLDatabase - maintain a connection to a PostgreSQL database
//
// .SECTION Description
//
// PostgreSQL (http://www.postgres.org) is a BSD-licensed SQL database.
// It's large, fast, and can not be easily embedded
// inside other applications.  Its databases are stored in files that
// belong to another process.
//
// This class provides a VTK interface to PostgreSQL.  You do need to
// download external libraries: we need a copy of PostgreSQL 8 and libpqxx.
//
// .SECTION Thanks
// Thanks to David Thompson from Sandia National Laboratories for implementing
// this class based on Andy Wilson's vtkSQLiteDatabase class.
//
// .SECTION See Also
// vtkPostgreSQLQuery

#ifndef __vtkPostgreSQLDatabase_h
#define __vtkPostgreSQLDatabase_h

#include "vtkSQLDatabase.h"

class vtkSQLQuery;
class vtkPostgreSQLQuery;
class vtkStringArray;
class vtkPostgreSQLDatabasePrivate;

class VTK_IO_EXPORT vtkPostgreSQLDatabase : public vtkSQLDatabase
{
  //BTX
  friend class vtkPostgreSQLQuery;
  friend class vtkPostgreSQLQueryPrivate;
  //ETX

public:
  vtkTypeRevisionMacro(vtkPostgreSQLDatabase, vtkSQLDatabase);
  void PrintSelf(ostream& os, vtkIndent indent);
  static vtkPostgreSQLDatabase *New();

  // Description:
  // Set/get the URL of the database.
  // PostgreSQL works by contacting a daemon over a socket, passing it requests,
  // and listening for responses. The actual database access is not done within
  // your process.
  //
  // The set method is implemented manually so that the modification time of the
  // URL and the Connection object may be tracked independently of the
  // vtkPostgreSQLDatabase object.
  //
  // The URL format for PostgreSQL databases is a true URL of the form:
  //   'psql://'[[username[':'password]'@']hostname[':'port]]'/'[dbname] .
  vtkSetStringMacro(URL);
  vtkGetStringMacro(URL);

  // Description:
  // Open a new connection to the database.  You need to set the
  // filename before calling this function.  Returns true if the
  // database was opened successfully; false otherwise.
  bool Open();

  // Description:
  // Close the connection to the database.
  void Close();
  
  // Description:
  // Return whether the database has an open connection
  bool IsOpen();

  // Description:
  // Return an empty query on this database.
  vtkSQLQuery* GetQueryInstance();
  
  // Description:
  // Get the last error text from the database
  const char* GetLastErrorText();
  
  // Description:
  // Get the list of tables from the database
  vtkStringArray* GetTables();
    
  // Description:
  // Get the list of fields for a particular table
  vtkStringArray* GetRecord( const char* table );

  // Description:
  // Return whether a feature is supported by the database.
  bool IsSupported( int feature );

  // Description:
  // Return a list of databases on the server.
  vtkStringArray* GetDatabases();

  // Description:
  // Create a new database, optionally dropping any existing database of the same name.
  // Returns true when the database is properly created and false on failure.
  bool CreateDatabase( const char* dbName, bool dropExisting = false );

  // Description:
  // Drop a database if it exists.
  // Returns true on success and false on failure.
  bool DropDatabase( const char* dbName );

protected:
  vtkPostgreSQLDatabase();
  ~vtkPostgreSQLDatabase();

  vtkTimeStamp URLMTime;
  vtkPostgreSQLDatabasePrivate* Connection;
  vtkTimeStamp ConnectionMTime;

private:
  vtkPostgreSQLDatabase(const vtkPostgreSQLDatabase &); // Not implemented.
  void operator=(const vtkPostgreSQLDatabase &); // Not implemented.
};

#endif // __vtkPostgreSQLDatabase_h

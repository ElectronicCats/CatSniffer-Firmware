/**
 * CatSerCommand - A modified version of the SerialCommands library
 * to tokenize and parse commands received over a serial port.
 *
 * Based on SerialCommands by:
 *   Copyright (C) 2012 Stefan Rado
 *   Copyright (C) 2011 Steven Cogswell <steven.cogswell@gmail.com>
 *                       http://husks.wordpress.com
 *
 * Modifications Copyright (C) 2025 Kevin Leon
 *
 * Version 1.0.0 (Modified)
 *
 * This library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "CatSerCommand.h"

/**
 * Constructor makes sure some things are set.
 */
CatSerCommand::CatSerCommand()
	: commandList(NULL)
	, commandCount(0)
	, defaultHandler(NULL)
	, term('\n')
	, // default terminator for commands, newline character
	last(NULL)
{
	strcpy(delim, " "); // strtok_r needs a null-terminated string
	clearBuffer();
}

/**
 * Adds a "command" and a handler function to the list of available commands.
 * This is used for matching a found token in the buffer, and gives the pointer
 * to the handler function to deal with it.
 */
void CatSerCommand::addCommand(const char *command, void (*function)())
{
	commandList = (SerialCommandCallback *)realloc(
		commandList,
		(commandCount + 1) * sizeof(SerialCommandCallback));
	strncpy(commandList[commandCount].command, command,
		SERIALCOMMAND_MAXCOMMANDLENGTH);
	commandList[commandCount].function = function;
	commandCount++;
}

/**
 * This sets up a handler to be called in the event that the receveived command
 * string isn't in the list of commands.
 */
void CatSerCommand::setDefaultHandler(void (*function)(const char *))
{
	defaultHandler = function;
}

/**
 * This checks the Serial stream for characters, and assembles them into a
 * buffer. When the terminator character (default '\n') is seen, it starts
 * parsing the buffer for a prefix command, and calls handlers setup by
 * addCommand() member
 */
void CatSerCommand::readSerial()
{
	while (Serial.available() > 0) {
		char inChar = Serial.read(); // Read single available character,
					     // there may be more waiting

		if (inChar == term) { // Check for the terminator (default '\r')
				      // meaning end of command
			char *command = strtok_r(buffer, delim,
						 &last); // Search for command
							 // at start of buffer
			if (command != NULL) {
				boolean matched = false;
				for (int i = 0; i < commandCount; i++) {
					// Compare the found command against the
					// list of known commands for a match
					if (strncmp(command,
						    commandList[i].command,
						    SERIALCOMMAND_MAXCOMMANDLENGTH) ==
					    0) {
						// Execute the stored handler
						// function for the command
						(*commandList[i].function)();
						matched = true;
						break;
					}
				}
				if (!matched && (defaultHandler != NULL)) {
					(*defaultHandler)(command);
				}
			}
			clearBuffer();
		} else if (isprint(inChar)) { // Only printable characters into
					      // the buffer
			if (bufPos < SERIALCOMMAND_BUFFER) {
				buffer[bufPos++] = inChar; // Put character into
							   // buffer
				buffer[bufPos] = '\0';	   // Null terminate
			}
		}
	}
}

/*
 * Clear the input buffer.
 */
void CatSerCommand::clearBuffer()
{
	buffer[0] = '\0';
	bufPos = 0;
}

/**
 * Retrieve the next token ("word" or "argument") from the command buffer.
 * Returns NULL if no more tokens exist.
 */
char *CatSerCommand::next()
{
	return strtok_r(NULL, delim, &last);
}

void CatSerCommand::showCommands()
{
	for (int i = 0; i < commandCount; i++) {
		Serial.println(commandList[i].command);
	}
}

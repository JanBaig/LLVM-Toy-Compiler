
#include <string>
#include <iostream>

// Scanner Code 

enum Token {
	tok_eof = -1,

	// commands/keywords 
	tok_def = -2,
	tok_extern = -3,

	// primary
	tok_identifier = -4,
	tok_number = -5
}; 

static std::string IdentifierStr; 
static double NumVal;

static int gettok() {
	static int LastChar = ' ';
	
	// Skip whitespace
	while (isspace(LastChar)) LastChar = getchar();

	// Get Identifier
	if (isalpha(LastChar)) {
		IdentifierStr = LastChar; 

		while (isalnum((LastChar = getchar()))) IdentifierStr += LastChar;
		
		// Match keywords
		if (IdentifierStr == "def") return tok_def; 
		if (IdentifierStr == "extern") return tok_extern; 
		
		// Not a keyword, must be an identifier
		return tok_identifier;
	} 

	// Get Numbers
	if (isdigit(LastChar) || LastChar == '.') { // Number: [0-9.]+ - CAN EXTEND 
		std::string NumStr; 
		do {
			NumStr += LastChar;
			LastChar = getchar();
		} while (isdigit(LastChar) || LastChar == '.'); 

		NumVal = strtod(NumStr.c_str(), nullptr);
		return tok_number;
	}

	// Skip Comments
	if (LastChar == '#') {
		// Ignore the entire line
		do {
			LastChar = getchar();
		} while (LastChar != EOF && LastChar != '\n' && LastChar != '\r'); 

		if (LastChar != EOF) return gettok();
	} 

	// We reached the end - no point in calling getchar() again
	if (LastChar == EOF) return tok_eof;  

	// Otherwise, just return the 'unknown' character as its ascii value
	// We haven't reached the end of input - therefore update LastChar so it now contains a new unprocessed character
	int ThisChar = LastChar; 
	LastChar = getchar();
	return ThisChar;
}

int main() { 
	while (true) {
		int tokenNum = gettok();
		std::cout << "Got: " << tokenNum << std::endl;
	} 
	// Does not output the EOF token
}


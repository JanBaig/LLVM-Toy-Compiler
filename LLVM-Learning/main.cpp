
#include <string>
#include <iostream>
#include <vector>

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

// Parser Code

class ExprAST {
public: 
	virtual ~ExprAST() = default;
}; 

class NumberExprAST : public ExprAST {
	double val;
public:
	NumberExprAST(double val) : val(val) {}
};

class VariableExprAST : public ExprAST {
	std::string Name;
public: 
	// Constant reference - 1) don't change value 2) don't make a copy of argument 
	VariableExprAST(const std::string& Name) : Name(Name) {}
};

class BinaryExprAST : public ExprAST {
	char op;
	std::unique_ptr<ExprAST> LHS, RHS;
public:
	BinaryExprAST(char op, std::unique_ptr<ExprAST> LHS, std::unique_ptr<ExprAST> RHS) :
		op(op), LHS(std::move(LHS)), RHS(std::move(RHS)) {};
};

class CallExprAST : public ExprAST {
	std::string Callee;
	std::vector<std::unique_ptr<ExprAST>> Args;
public:
	CallExprAST(const std::string Callee, std::vector<std::unique_ptr<ExprAST>> Args) :
		Callee(Callee), Args(std::move(Args)) {};
}; 

class PrototypeAST {
	std::string Name;
	std::vector<std::string> Args; // implicit argument number 
public: 
	PrototypeAST(const std::string& Name, std::vector<std::string> Args) :
		Name(Name), Args(std::move(Args)) {}; 

	// Why repeat const here?
	const std::string& getName() const { return Name; }
};

class FunctionAST {
	std::unique_ptr<PrototypeAST> proto;
	std::unique_ptr<ExprAST> body;
public:
	FunctionAST(std::unique_ptr<PrototypeAST> proto, std::unique_ptr<ExprAST> body) :
		proto(std::move(proto)), body(std::move(body)) {};
};

int main() { 

	std::cout << "Enter Syntax: \n" << std::endl;
	while (true) {
		int tokenNum = gettok();
		std::cout << "Got: " << tokenNum << std::endl; 

		// When Ctrl-Z + Enter is hit
		if (tokenNum == EOF) break; 
	}
}

// Learn about move semantics & (r/l)values 


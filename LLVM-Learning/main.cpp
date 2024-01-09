
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

// AST Classes

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
	std::vector<std::string> Args;
public: 
	// Type of arguments doesn't need to be stored anywhere since they are all 
	// the same type in kaleidoscope - doubles FP values (the only values that exists)
	// Therefore, vec of strings just stores the name of the args -> Double <name> ...
	PrototypeAST(const std::string& Name, std::vector<std::string> Args) :
		Name(Name), Args(std::move(Args)) {}; 

	const std::string& getName() const { return Name; }
};

class FunctionAST {
	std::unique_ptr<PrototypeAST> proto;
	std::unique_ptr<ExprAST> body;
public:
	FunctionAST(std::unique_ptr<PrototypeAST> proto, std::unique_ptr<ExprAST> body) :
		proto(std::move(proto)), body(std::move(body)) {};
};

// Parser 

// Parser Helpers

static int currTok;
static int getNextToken() { return currTok = gettok(); }

std::unique_ptr<ExprAST> LogError(const char* str) {
	fprintf(stderr, "Error: %s\n", str);
	return nullptr;
}

std::unique_ptr<PrototypeAST> LogErrorP(const char* str) {
	LogError(str);
	return nullptr;
}

// Parser Functions

static std::unique_ptr<ExprAST> ParseNumberExpr() {
	auto Result = std::make_unique<NumberExprAST>(NumVal);
	getNextToken(); // consume current, advances lexer to next token
	return std::move(Result);
}

static std::unique_ptr<ExprAST> ParseParenExpr() {
	getNextToken(); // consume '(' 
	
	auto V = ParseExpresion();
	
	if (!V) return nullptr; 
	if (currTok != ')') return LogError("expected ')'");

	getNextToken(); // consume ')', advance lexer currTok
	return V;
}

static std::unique_ptr<ExprAST> ParseIdentifierExpr() {
	std::string idName = IdentifierStr; 
	getNextToken();  // consume identifier, advance currTok 

	// Just a variable reference
	if (currTok != '(') return std::make_unique<VariableExprAST>(idName);

	// Possible function call
	getNextToken(); // consume '('
	std::vector<std::unique_ptr<ExprAST>> Args; 
	if (currTok != ')') {
		while (true) {
			// If no errors found when parsing the current arg
			if (auto Arg = ParseExpression()) Args.push_back(std::move(Arg));
			else return nullptr;
			
			if (currTok == ')') break;
			if (currTok != ',') return LogError("Expected ')' or ',' in the argument list");
			getNextToken(); // consume ',', advance
		}
	}

	getNextToken(); // consume ')', advance
	return std::make_unique<CallExprAST>(idName, std::move(Args));
}

static std::unique_ptr<ExprAST> ParsePrimary() { 
	switch (currTok) {
		default: return LogError("unknown token when expecting an expression");
		case tok_identifier: return ParseIdentifierExpr();
		case tok_number: return ParseNumberExpr();
		case '(': return ParseParenExpr();
	}
}


int main() { 

	std::cout << "Enter Syntax: \n" << std::endl; 

	while (true) {
		int tokenNum = gettok();
		std::cout << "Got: " << tokenNum << std::endl; 

		// When Ctrl-Z + Enter is hit
		if (tokenNum == EOF) break; 
	} 
}



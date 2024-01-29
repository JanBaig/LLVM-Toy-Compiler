
#include "/usr/share/doc/llvm-14-examples/examples/Kaleidoscope/include/KaleidoscopeJIT.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace llvm;
using namespace llvm::orc;

//===----------------------------------------------------------------------===//
// Lexer
//===----------------------------------------------------------------------===//

enum Token {
    tok_eof = -1,

    // commands
    tok_def = -2,
    tok_extern = -3,

    // primary
    tok_identifier = -4,
    tok_number = -5
};

// Metadata for tokens
static std::string IdentifierStr; 
static double NumVal;             

static int gettok() {
    // Retains value between function calls
    static int LastChar = ' ';

    // Skip any whitespace.
    while (isspace(LastChar)) LastChar = getchar();

    // Might be keyword or identifier
    if (isalpha(LastChar)) { 
        IdentifierStr = LastChar;
        while (isalnum((LastChar = getchar())))
            IdentifierStr += LastChar;

        // Check if keyword
        if (IdentifierStr == "def")
            return tok_def;
        if (IdentifierStr == "extern")
            return tok_extern;

        // Not a keyword, must be user-defined identifier
        return tok_identifier;
    }

    // Is a number - EXTEND to avoid [0-9.]+
    if (isdigit(LastChar) || LastChar == '.') { 
        std::string NumStr;
        do {
            NumStr += LastChar;
            LastChar = getchar();
        } while (isdigit(LastChar) || LastChar == '.');

        NumVal = strtod(NumStr.c_str(), nullptr);
        return tok_number;
    }

    // Is comment
    if (LastChar == '#') {
        
        do {
            LastChar = getchar();
        } while (LastChar != EOF && LastChar != '\n' && LastChar != '\r');

        // Newline encountered, recursively scan for more tokens
        if (LastChar != EOF)
            return gettok();
    }

    if (LastChar == EOF)
        return tok_eof;

    // Return current char as its ASCII value 
    // Update 'LastChar' due to it's static nature - next call to gettok() works with updated value
    int ThisChar = LastChar;
    LastChar = getchar();
    return ThisChar;
}

//===----------------------------------------------------------------------===//
// Abstract Syntax Tree (aka Parse Tree)
//===----------------------------------------------------------------------===//

namespace {

    class ExprAST {
    public:
        virtual ~ExprAST() = default;

        virtual Value* codegen() = 0;
    };

    class NumberExprAST : public ExprAST {
        double Val;
    public:
        NumberExprAST(double Val) : Val(Val) {}

        Value* codegen() override;
    };

    class VariableExprAST : public ExprAST {
        std::string Name;
    public:
        VariableExprAST(const std::string& Name) : Name(Name) {}

        Value* codegen() override;
    };

    class BinaryExprAST : public ExprAST {
        char Op;
        std::unique_ptr<ExprAST> LHS, RHS;
    public:
        BinaryExprAST(char Op, std::unique_ptr<ExprAST> LHS,
            std::unique_ptr<ExprAST> RHS)
            : Op(Op), LHS(std::move(LHS)), RHS(std::move(RHS)) {}

        Value* codegen() override;
    };

    // Function Calling
    class CallExprAST : public ExprAST {
        std::string Callee; // Function Name
        std::vector<std::unique_ptr<ExprAST>> Args; // Arg types not stored since every Value is assumed to be a DP FP number 
    public:
        CallExprAST(const std::string& Callee,
            std::vector<std::unique_ptr<ExprAST>> Args)
            : Callee(Callee), Args(std::move(Args)) {}

        // Not calling Function* here because a call expression produces a value and not a functions
        Value* codegen() override;
    };

    // Function Declaration
    class PrototypeAST {
        std::string Name;
        std::vector<std::string> Args;
    public:
        PrototypeAST(const std::string& Name, std::vector<std::string> Args)
            : Name(Name), Args(std::move(Args)) {}

        Function* codegen();
        const std::string& getName() const { return Name; }
    };

    // Function Definition
    class FunctionAST {
        std::unique_ptr<PrototypeAST> Proto;
        std::unique_ptr<ExprAST> Body;
    public:
        FunctionAST(std::unique_ptr<PrototypeAST> Proto, std::unique_ptr<ExprAST> Body)
            : Proto(std::move(Proto)), Body(std::move(Body)) {}
        
        Function* codegen();
    };
} 

//===----------------------------------------------------------------------===//
// Parser
//===----------------------------------------------------------------------===//

static int CurTok;
static int getNextToken() { return CurTok = gettok(); }

static std::map<char, int> BinopPrecedence;

static int GetTokPrecedence() {
    // Operator tokens are ASCII values
    if (!isascii(CurTok)) return -1;

    int TokPrec = BinopPrecedence[CurTok];
    if (TokPrec <= 0)
        return -1;
    return TokPrec;
}

std::unique_ptr<ExprAST> LogError(const char* Str) {
    fprintf(stderr, "Error: %s\n", Str);
    return nullptr;
}

std::unique_ptr<PrototypeAST> LogErrorP(const char* Str) {
    LogError(Str);
    return nullptr;
}

static std::unique_ptr<ExprAST> ParseExpression();

static std::unique_ptr<ExprAST> ParseNumberExpr() {
    auto Result = std::make_unique<NumberExprAST>(NumVal);
    getNextToken(); // consume number
    return std::move(Result);
}

static std::unique_ptr<ExprAST> ParseParenExpr() {
    getNextToken(); // consume '(' 
    auto V = ParseExpression();
    if (!V)
        return nullptr;

    if (CurTok != ')') return LogError("expected ')'");
    getNextToken(); // consume ')'
    return V;
}

static std::unique_ptr<ExprAST> ParseIdentifierExpr() {
    std::string IdName = IdentifierStr;

    getNextToken(); 

    // Just a variable and not a call expression
    if (CurTok != '(') 
        return std::make_unique<VariableExprAST>(IdName);

    getNextToken(); // consume '(' 

    // Parse args of call expression
    std::vector<std::unique_ptr<ExprAST>> Args;
    if (CurTok != ')') {
        while (true) {
            if (auto Arg = ParseExpression())
                Args.push_back(std::move(Arg));
            else
                return nullptr;

            if (CurTok == ')')
                break;

            if (CurTok != ',')
                return LogError("Expected ')' or ',' in argument list");
            getNextToken();
        }
    }

    getNextToken(); // consume ')'

    return std::make_unique<CallExprAST>(IdName, std::move(Args));
}

static std::unique_ptr<ExprAST> ParsePrimary() {
    // CurTok allows for lookahead
    switch (CurTok) {
    default:
        return LogError("unknown token when expecting an expression");
    case tok_identifier:
        return ParseIdentifierExpr();
    case tok_number:
        return ParseNumberExpr();
    case '(':
        return ParseParenExpr();
    }
}

static std::unique_ptr<ExprAST> ParseBinOpRHS(int ExprPrec, std::unique_ptr<ExprAST> LHS) {
    while (true) {
        int TokPrec = GetTokPrecedence();

        // Current operator does not have a high precedence than the previous one
        if (TokPrec < ExprPrec)
            return LHS;

        int BinOp = CurTok;
        getNextToken(); // consume the operator

        auto RHS = ParsePrimary();
        if (!RHS)
            return nullptr;

        int NextPrec = GetTokPrecedence();
        if (TokPrec < NextPrec) {
            RHS = ParseBinOpRHS(TokPrec + 1, std::move(RHS));
            if (!RHS)
                return nullptr;
        }

        LHS = std::make_unique<BinaryExprAST>(BinOp, std::move(LHS), std::move(RHS));
    }
}

static std::unique_ptr<ExprAST> ParseExpression() {
    auto LHS = ParsePrimary();
    if (!LHS)
        return nullptr;

    return ParseBinOpRHS(0, std::move(LHS));
}

static std::unique_ptr<PrototypeAST> ParsePrototype() {
    if (CurTok != tok_identifier)
        return LogErrorP("Expected function name in prototype");

    std::string FnName = IdentifierStr;
    getNextToken(); // consume identifier

    if (CurTok != '(')
        return LogErrorP("Expected '(' in prototype");

    std::vector<std::string> ArgNames;
    while (getNextToken() == tok_identifier)
        ArgNames.push_back(IdentifierStr);
    if (CurTok != ')')
        return LogErrorP("Expected ')' in prototype");

    getNextToken(); // consume ')'

    return std::make_unique<PrototypeAST>(FnName, std::move(ArgNames));
}

static std::unique_ptr<FunctionAST> ParseDefinition() {
    getNextToken(); // consume 'def'
    auto Proto = ParsePrototype();
    if (!Proto)
        return nullptr;

    if (auto E = ParseExpression())
        return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
    return nullptr;
}

static std::unique_ptr<FunctionAST> ParseTopLevelExpr() {
    if (auto E = ParseExpression()) {
        // Make an anonymous proto (no args)
        auto Proto = std::make_unique<PrototypeAST>("__anon_expr",
            std::vector<std::string>());
        return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
    }
    return nullptr;
}

static std::unique_ptr<PrototypeAST> ParseExtern() {
    getNextToken(); // eat extern
    return ParsePrototype();
}

//===----------------------------------------------------------------------===//
// Code Generation
//===----------------------------------------------------------------------===//

static std::unique_ptr<LLVMContext> TheContext;
static std::unique_ptr<Module> TheModule;
static std::unique_ptr<IRBuilder<>> Builder;
static std::map<std::string, Value*> NamedValues;
static std::unique_ptr<legacy::FunctionPassManager> TheFPM;
static std::unique_ptr<KaleidoscopeJIT> TheJIT;
static std::map<std::string, std::unique_ptr<PrototypeAST>> FunctionProtos;
static ExitOnError ExitOnErr;

Value* LogErrorV(const char* Str) {
    LogError(Str);
    return nullptr;
}

Value* NumberExprAST::codegen() {
    // We use get:: to avoid having different variables point to identical valued constants
    // More memory efficient to have a single variable and reuse that
    return ConstantFP::get(*TheContext, APFloat(Val));
}

// Variable Reference
Value* VariableExprAST::codegen() {
    // Look this variable up in the function.
    Value* V = NamedValues[Name];
    if (!V)
        return LogErrorV("Unknown variable name");
    return V;
}

Value* BinaryExprAST::codegen() {
    Value* L = LHS->codegen();
    Value* R = RHS->codegen();
    if (!L || !R)
        return nullptr;

    switch (Op) {
    case '+':
        return Builder->CreateFAdd(L, R, "addtmp");
    case '-':
        return Builder->CreateFSub(L, R, "subtmp");
    case '*':
        return Builder->CreateFMul(L, R, "multmp");
    case '<':
        L = Builder->CreateFCmpULT(L, R, "cmptmp");
        // Convert bool 0/1 to double 0.0 or 1.0
        return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
    default:
        return LogErrorV("invalid binary operator");
    }
}
  
Value* CallExprAST::codegen() {
    // Look up the name in the Module's symbol table
    Function* CalleeF = TheModule->getFunction(Callee);
    if (!CalleeF)
        return LogErrorV("Unknown function referenced");

    // CalleeF is the callee's function definition
    // The Args refer to the the arguments passed in when a call to CalleeF is made
    // Args.size() depends on user input for calling, CalleeF depends on the function definition
    if (CalleeF->arg_size() != Args.size())
        return LogErrorV("Incorrect # arguments passed");

    // Codegen'ing call arguments
    std::vector<Value*> ArgsV;
    for (unsigned i = 0, e = Args.size(); i != e; ++i) {
        ArgsV.push_back(Args[i]->codegen());
        if (!ArgsV.back())
            return nullptr;
    }

    return Builder->CreateCall(CalleeF, ArgsV, "calltmp");
}

// Declare Function Signature
Function* PrototypeAST::codegen() {
    // Create a vector of Type pointers for function arguments, all of type double
    std::vector<Type*> Doubles(Args.size(), Type::getDoubleTy(*TheContext));
    
    // Create a FunctionType representing a function returning double and taking
    // arguments of double types based on the vector created above
    FunctionType* FT = 
        FunctionType::get(Type::getDoubleTy(*TheContext), Doubles, false);

    // Create a new Function instance with ExternalLinkage, the given name, and
    // associated with the current module (TheModule)
    // Implicitly generated IR for function args
    // .get() retrives the raw pointer from TheModule to pass into the function
    // The name is registered in TheModule's symbol tables
    Function* F =
        Function::Create(FT, Function::ExternalLinkage, Name, TheModule.get());

    // Set names (parameter names) for function arguments based on the names provided in the Args vector
    unsigned Idx = 0;
    for (auto& Arg : F->args())
        Arg.setName(Args[Idx++]);

    // Return the generated LLVM function instance
    // Function signitures in LLVM = functions but functions without bodies
    return F;
}

Function* FunctionAST::codegen() {
    // Try to get the existing function from the module
    Function* TheFunction = TheModule->getFunction(Proto->getName());

    // If the function DNE, generate its prototype
    if (!TheFunction)
        TheFunction = Proto->codegen();

    // If the prototype cannot be generated, return a nullptr
    if (!TheFunction)
        return nullptr;

    // Create a basic block named 'entry' in the function
    BasicBlock* BB = BasicBlock::Create(*TheContext, "entry", TheFunction);
    
    // Set the insertion point for the IRBuilder to the new basic block 'entry'
    Builder->SetInsertPoint(BB);

    // Clear the map of NamedValues in the current scope (NamedValues could hold another function's values)
    NamedValues.clear();

    // Adding function parameters into the NamedValues map so that they can be resolved within the function body
    // Their value in the map is an address that comes from the LLVM function's arg list. 
    // The LLVM function's arg list is updated with the values of the codegen'd arguments when it is called 
    // Since we passed an address to NamedValues, the above change is reflected within the map
    for (auto& Arg : TheFunction->args())
        NamedValues[std::string(Arg.getName())] = &Arg;
    
    // Generate code for the body of the function
    if (Value* RetVal = Body->codegen()) {
        // If the body code generation is successful, create a return instruction
        Builder->CreateRet(RetVal);

        // Verify the function to ensure it is well-formed
        verifyFunction(*TheFunction);

        // Optimize the function
        TheFPM->run(*TheFunction);

        // Return the generated function
        return TheFunction;
    }

    // If the body code generation is unsuccessful, erase the function and return nullptr
    // If we didn’t delete it, it would live in the symbol table, with a body, preventing future redefinition.
    TheFunction->eraseFromParent();
    return nullptr;
}

//===----------------------------------------------------------------------===//
// Top-Level parsing and JIT Driver
//===----------------------------------------------------------------------===//

static void InitializeModuleAndPassManager() {
    // Open a new context and module.
    TheContext = std::make_unique<LLVMContext>();
    TheModule = std::make_unique<Module>("my cool jit", *TheContext);
    TheModule->setDataLayout(TheJIT->getDataLayout());

    // Create a new builder for the module.
    Builder = std::make_unique<IRBuilder<>>(*TheContext);

    // Create a new pass manager attached to it.
    TheFPM = std::make_unique<legacy::FunctionPassManager>(TheModule.get());

    // Do simple "peephole" optimizations and bit-twiddling optzns.
    TheFPM->add(createInstructionCombiningPass());
    // Reassociate expressions.
    TheFPM->add(createReassociatePass());
    // Eliminate Common SubExpressions.
    TheFPM->add(createGVNPass());
    // Simplify the control flow graph (deleting unreachable blocks, etc).
    TheFPM->add(createCFGSimplificationPass());

    TheFPM->doInitialization();
}

static void HandleDefinition() {
    if (auto FnAST = ParseDefinition()) {
        if (auto* FnIR = FnAST->codegen()) {
            fprintf(stderr, "Read function definition:");
            FnIR->print(errs());
            fprintf(stderr, "\n");
        }
    }
    else {
        getNextToken();
    }
}

static void HandleExtern() {
    if (auto ProtoAST = ParseExtern()) {
        if (auto* FnIR = ProtoAST->codegen()) {
            fprintf(stderr, "Read extern: ");
            FnIR->print(errs());
            fprintf(stderr, "\n");
        }
    }
    else {
        getNextToken();
    }
}

static void HandleTopLevelExpression() {
    if (auto FnAST = ParseTopLevelExpr()) {
        if (auto* FnIR = FnAST->codegen()) {
            fprintf(stderr, "Read top-level expression:");
            FnIR->print(errs());
            fprintf(stderr, "\n");

            FnIR->eraseFromParent();
        }
    }
    else {
        getNextToken();
    }
}

static void MainLoop() {
    while (true) {
        fprintf(stderr, "ready> ");
        switch (CurTok) {
        case tok_eof:
            return;
        case ';': 
            getNextToken();
            break;
        case tok_def:
            HandleDefinition();
            break;
        case tok_extern:
            HandleExtern();
            break;
        default:
            HandleTopLevelExpression();
            break;
        }
    }
}

//===----------------------------------------------------------------------===//
// Main driver code.
//===----------------------------------------------------------------------===//

int main() {
    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();
    InitializeNativeTargetAsmParser();

    BinopPrecedence['<'] = 10;
    BinopPrecedence['+'] = 20;
    BinopPrecedence['-'] = 20;
    BinopPrecedence['*'] = 40; 

    fprintf(stderr, "ready> ");
    getNextToken();

    TheJIT = ExitOnErr(KaleidoscopeJIT::Create());
    InitializeModuleAndPassManager();

    MainLoop();

    TheModule->print(errs(), nullptr);

    return 0;
}

// There's a module symbol table (function names) and a namedMap (function args) symbol tabnle


// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "MaterialRegex.h"

#include <cctype>
#include <cstring>
#include <new>

namespace
{
    constexpr int GNullNode = -1;
    constexpr int GDummyNode = -2;

    // 06ed0ab0: links are unsigned, big-endian, and BACK reverses direction.
    int NextRegexNode(const unsigned char* Program, int Node)
    {
        if (Node == GDummyNode)
            return GNullNode;
        const unsigned int Offset = (Program[Node + 1] << 8) | Program[Node + 2];
        if (Offset == 0)
            return GNullNode;
        return Program[Node] == 7 ? Node - (int)Offset
                                  : Node + (int)Offset;
    }

    bool IsRegexQuantifier(char Character)
    {
        return Character == '*' || Character == '+' || Character == '?';
    }

    struct FRegexCompiler
    {
        const char* Parse;
        unsigned char* Program;
        int Cursor = 0;
        int Size = 0;
        int GroupNumber = 1;

        // 06ed0400 / 06ed0440: pass one counts without constructing links.
        int EmitNode(unsigned char Opcode)
        {
            if (Program == nullptr)
            {
                Size += 3;
                return GDummyNode;
            }
            const int Node = Cursor;
            Program[Cursor++] = Opcode;
            Program[Cursor++] = 0;
            Program[Cursor++] = 0;
            return Node;
        }

        void EmitByte(unsigned char Character)
        {
            if (Program == nullptr)
                ++Size;
            else
                Program[Cursor++] = Character;
        }

        // 06ed0460: insert the three-byte prefix before an existing atom.
        void InsertNode(unsigned char Opcode, int Node)
        {
            if (Program == nullptr)
            {
                Size += 3;
                return;
            }
            for (int Index = Cursor; Index > Node; --Index)
                Program[Index + 2] = Program[Index - 1];
            Cursor += 3;
            Program[Node] = Opcode;
            Program[Node + 1] = 0;
            Program[Node + 2] = 0;
        }

        // 06ed04b0: patch the final link, with the BACK sign convention.
        void PatchTail(int Node, int Target)
        {
            if (Node == GDummyNode)
                return;
            int Next = NextRegexNode(Program, Node);
            while (Next != GNullNode)
            {
                Node = Next;
                Next = NextRegexNode(Program, Node);
            }
            const int Offset = Program[Node] == 7 ? Node - Target : Target - Node;
            Program[Node + 1] = (unsigned char)(Offset >> 8);
            Program[Node + 2] = (unsigned char)Offset;
        }

        // 06ed0510: only branch nodes have an operand tail to patch.
        void PatchOperandTail(int Node, int Target)
        {
            if (Node != GNullNode && Node != GDummyNode && Program[Node] == 6)
                PatchTail(Node + 3, Target);
        }

        // 06ecfc30: parentheses, newline/pipe alternatives and closing nodes.
        int CompileExpression(bool bParenthesized, unsigned int& Flags)
        {
            Flags = 1;
            int Group = 0;
            int Result = GNullNode;
            if (bParenthesized)
            {
                if (GroupNumber >= 10)
                    return GNullNode;
                Group = GroupNumber++;
                Result = EmitNode((unsigned char)(20 + Group));
            }
            unsigned int BranchFlags;
            int Branch = CompileBranch(BranchFlags);
            if (Branch == GNullNode)
                return GNullNode;
            if (Result != GNullNode)
                PatchTail(Result, Branch);
            else
                Result = Branch;
            for (;;)
            {
                if ((BranchFlags & 1) == 0)
                    Flags &= ~1u;
                Flags |= BranchFlags & 4;
                if (*Parse != '|' && *Parse != '\n')
                    break;
                ++Parse;
                Branch = CompileBranch(BranchFlags);
                if (Branch == GNullNode)
                    return GNullNode;
                PatchTail(Result, Branch);
            }
            const int End = EmitNode((unsigned char)(bParenthesized ? 30 + Group : 0));
            PatchTail(Result, End);
            for (int Node = Result; Node != GNullNode; Node = NextRegexNode(Program, Node))
                PatchOperandTail(Node, End);
            if (bParenthesized)
            {
                const char Closing = *Parse++;
                if (Closing != ')')
                    return GNullNode;
            }
            else if (*Parse != '\0')
                return GNullNode;
            return Result;
        }

        // 06ecfd50: concatenation; an empty branch owns a NOTHING operand.
        int CompileBranch(unsigned int& Flags)
        {
            Flags = 0;
            const int Result = EmitNode(6);
            int Previous = GNullNode;
            while (*Parse != '\0' && *Parse != ')' && *Parse != '\n' && *Parse != '|')
            {
                unsigned int PieceFlags;
                const int Piece = CompilePiece(PieceFlags);
                if (Piece == GNullNode)
                    return GNullNode;
                Flags |= PieceFlags & 1;
                if (Previous == GNullNode)
                    Flags |= PieceFlags & 4;
                else
                    PatchTail(Previous, Piece);
                Previous = Piece;
            }
            if (Previous == GNullNode)
                EmitNode(9);
            return Result;
        }

        // 06ecfdf0: simple repetitions use STAR/PLUS; composite ones branch back.
        int CompilePiece(unsigned int& Flags)
        {
            unsigned int AtomFlags;
            const int Result = CompileAtom(AtomFlags);
            if (Result == GNullNode)
                return GNullNode;
            const char Quantifier = *Parse;
            if (!IsRegexQuantifier(Quantifier))
            {
                Flags = AtomFlags;
                return Result;
            }
            if ((AtomFlags & 1) == 0 && Quantifier != '?')
                return GNullNode;
            Flags = Quantifier == '+' ? 1 : 4;
            if (Quantifier == '*')
            {
                if ((AtomFlags & 2) != 0)
                    InsertNode(10, Result);
                else
                {
                    InsertNode(6, Result);
                    const int Back = EmitNode(7);
                    PatchOperandTail(Result, Back);
                    PatchOperandTail(Result, Result);
                    const int Branch = EmitNode(6);
                    PatchTail(Result, Branch);
                    const int Nothing = EmitNode(9);
                    PatchTail(Result, Nothing);
                }
            }
            else if (Quantifier == '+')
            {
                if ((AtomFlags & 2) != 0)
                    InsertNode(11, Result);
                else
                {
                    const int Branch = EmitNode(6);
                    PatchTail(Result, Branch);
                    const int Back = EmitNode(7);
                    PatchTail(Back, Result);
                    const int Alternate = EmitNode(6);
                    PatchTail(Branch, Alternate);
                    const int Nothing = EmitNode(9);
                    PatchTail(Result, Nothing);
                }
            }
            else
            {
                InsertNode(6, Result);
                const int Branch = EmitNode(6);
                PatchTail(Result, Branch);
                const int Nothing = EmitNode(9);
                PatchTail(Result, Nothing);
                PatchOperandTail(Result, Nothing);
            }
            ++Parse;
            return IsRegexQuantifier(*Parse) ? GNullNode : Result;
        }

        // 06ecff60: this is the retail dialect, including its class-only escapes.
        int CompileAtom(unsigned int& Flags)
        {
            Flags = 0;
            const char Character = *Parse++;
            switch (Character)
            {
            case '^': return EmitNode(1);
            case '$': return EmitNode(2);
            case '.': Flags = 3; return EmitNode(3);
            case '(':
            {
                unsigned int GroupFlags;
                const int Group = CompileExpression(true, GroupFlags);
                if (Group != GNullNode)
                    Flags |= GroupFlags & 5;
                return Group;
            }
            case '[':
            {
                const bool bNegated = *Parse == '^';
                const int Result = EmitNode(bNegated ? 5 : 4);
                if (bNegated)
                    ++Parse;
                if (*Parse == ']' || *Parse == '-')
                    EmitByte((unsigned char)*Parse++);
                while (*Parse != '\0' && *Parse != ']')
                {
                    const char ClassCharacter = *Parse++;
                    if (ClassCharacter == '-')
                    {
                        if (*Parse == ']' || *Parse == '\0')
                            EmitByte('-');
                        else
                        {
                            unsigned int First = (unsigned char)Parse[-2] + 1u;
                            const unsigned int Last = (unsigned char)*Parse;
                            if (First > Last + 1)
                                return GNullNode;
                            for (; First <= Last; ++First)
                                EmitByte((unsigned char)First);
                            ++Parse;
                        }
                    }
                    else if (ClassCharacter == '\\')
                    {
                        const char Escape = *Parse++;
                        switch (Escape)
                        {
                        case '-': EmitByte('-'); break;
                        case '\\': EmitByte('\\'); break;
                        case 'n': EmitByte('\n'); break;
                        case 'r': EmitByte('\r'); break;
                        case 't': EmitByte('\t'); break;
                        case 's':
                            EmitByte(' '); EmitByte('\t'); EmitByte('\n'); EmitByte('\r');
                            break;
                        case 'd':
                            for (unsigned char Digit = '0'; Digit <= '9'; ++Digit)
                                EmitByte(Digit);
                            break;
                        default: break; // Retail consumes unknown escapes without emitting.
                        }
                    }
                    else
                        EmitByte((unsigned char)ClassCharacter);
                }
                EmitByte(0);
                if (*Parse != ']')
                    return GNullNode;
                ++Parse;
                Flags = 3;
                return Result;
            }
            case '\0': case '\n': case ')': case '*': case '+': case '?': case '|':
                return GNullNode;
            case '\\':
                if (*Parse == '\0')
                {
                    ++Parse;
                    return GNullNode;
                }
                if (*Parse == '<')
                {
                    ++Parse;
                    return EmitNode(12);
                }
                if (*Parse == '>')
                {
                    ++Parse;
                    return EmitNode(13);
                }
                ++Parse;
                break;
            default: break;
            }
            --Parse;
            const int Result = EmitNode(8);
            const char* Previous = nullptr;
            for (;;)
            {
                const char* SavedPrevious = Previous;
                const unsigned char Literal = (unsigned char)*Parse++;
                const char Next = *Parse;
                if (Next == '\\')
                {
                    EmitByte(Literal);
                    if (Parse[1] == '\0' || Parse[1] == '<' || Parse[1] == '>')
                        break;
                    Previous = Parse++;
                }
                else if (IsRegexQuantifier(Next))
                {
                    if (SavedPrevious != nullptr)
                        Parse = SavedPrevious;
                    else
                        EmitByte(Literal);
                    Previous = SavedPrevious;
                    break;
                }
                else if (Next == '\0' || Next == '\n' || Next == '$' || Next == '(' ||
                         Next == ')' || Next == '.' || Next == '[' || Next == '^' || Next == '|')
                {
                    EmitByte(Literal);
                    Previous = SavedPrevious;
                    break;
                }
                else
                {
                    EmitByte(Literal);
                    Previous = Parse;
                }
            }
            EmitByte(0);
            Flags = Previous == nullptr ? 3 : 1;
            return Result;
        }
    };

    struct FRegexExecutor
    {
        const unsigned char* Program;
        const char* Input;
        const char* Begin;
        const char* Starts[10] = {};
        const char* Ends[10] = {};

        // 06ed09c0: maximal consumption of simple repeat operands.
        int Repeat(int Node)
        {
            const char* Scan = Input;
            const char* Operand = (const char*)(Program + Node + 3);
            switch (Program[Node])
            {
            case 3: Scan += std::strlen(Scan); break;
            case 4:
                while (*Scan != '\0' && std::strchr(Operand, *Scan) != nullptr)
                    ++Scan;
                break;
            case 5:
                while (*Scan != '\0' && std::strchr(Operand, *Scan) == nullptr)
                    ++Scan;
                break;
            case 8:
                while (*Scan == *Operand)
                    ++Scan;
                break;
            default: break;
            }
            const int Count = (int)(Scan - Input);
            Input = Scan;
            return Count;
        }

        // 06ed06a0: ordered recursive backtracking with deferred capture writes.
        bool Match(int Node)
        {
            while (Node != GNullNode)
            {
                int Next = NextRegexNode(Program, Node);
                const char* Saved = Input;
                const unsigned char Opcode = Program[Node];
                const char* Operand = (const char*)(Program + Node + 3);
                switch (Opcode)
                {
                case 0: return true;
                case 1: if (Input != Begin) return false; break;
                case 2: if (*Input != '\0') return false; break;
                case 3: if (*Input == '\0') return false; ++Input; break;
                case 4:
                    if (*Input == '\0' || std::strchr(Operand, *Input) == nullptr)
                        return false;
                    ++Input; break;
                case 5:
                    if (*Input == '\0' || std::strchr(Operand, *Input) != nullptr)
                        return false;
                    ++Input; break;
                case 6:
                    if (Program[Next] != 6)
                        Next = Node + 3;
                    else
                    {
                        do
                        {
                            Saved = Input;
                            if (Match(Node + 3))
                                return true;
                            Input = Saved;
                            Node = NextRegexNode(Program, Node);
                        } while (Node != GNullNode && Program[Node] == 6);
                        return false;
                    }
                    break;
                case 7: case 9: break;
                case 8:
                {
                    if (*Operand != *Input)
                        return false;
                    const size_t Length = std::strlen(Operand);
                    if (Length > 1 && std::strncmp(Operand, Input, Length) != 0)
                        return false;
                    Input += Length;
                    break;
                }
                case 10: case 11:
                {
                    const char NextCharacter = Program[Next] == 8
                        ? (char)Program[Next + 3] : '\0';
                    int Count = Repeat(Node + 3);
                    const int Minimum = Opcode == 10 ? 0 : 1;
                    for (; Count >= Minimum; --Count)
                    {
                        if ((NextCharacter == '\0' || *Input == NextCharacter) && Match(Next))
                            return true;
                        Input = Saved + Count - 1;
                    }
                    return false;
                }
                case 12:
                    if (std::isalnum(*Input) == 0 && *Input != '_')
                        return false;
                    if (Input > Begin && (std::isalnum(Input[-1]) != 0 || Input[-1] == '_'))
                        return false;
                    break;
                case 13:
                    if (std::isalnum(*Input) != 0 || *Input == '_')
                        return false;
                    break;
                default:
                    if (Opcode >= 21 && Opcode <= 29)
                    {
                        if (!Match(Next))
                            return false;
                        if (Starts[Opcode - 20] == nullptr)
                            Starts[Opcode - 20] = Saved;
                        return true;
                    }
                    if (Opcode >= 31 && Opcode <= 39)
                    {
                        if (!Match(Next))
                            return false;
                        if (Ends[Opcode - 30] == nullptr)
                            Ends[Opcode - 30] = Saved;
                        return true;
                    }
                    return false;
                }
                Node = Next;
            }
            return false;
        }

        // 06ed0630: every attempted start clears all ten capture pairs.
        bool Try(const char* Start)
        {
            Input = Start;
            for (int Index = 0; Index < 10; ++Index)
            {
                Starts[Index] = nullptr;
                Ends[Index] = nullptr;
            }
            if (!Match(1))
                return false;
            Starts[0] = Start;
            Ends[0] = Input;
            return true;
        }
    };
}

// 06ecfae0: count pass, bounded allocation, emit pass, then search hints.
std::unique_ptr<FMaterialRegex> FMaterialRegex::Compile(const char* Pattern)
{
    if (Pattern == nullptr)
        return nullptr;
    FRegexCompiler Count = { Pattern, nullptr };
    Count.EmitByte(0x9c);
    unsigned int Flags;
    if (Count.CompileExpression(false, Flags) == GNullNode || Count.Size >= 0x7fff)
        return nullptr;
    std::unique_ptr<FMaterialRegex> Result(new (std::nothrow) FMaterialRegex);
    if (Result == nullptr)
        return nullptr;
    try
    {
        Result->Program.resize(Count.Size);
    }
    catch (const std::bad_alloc&)
    {
        return nullptr;
    }
    FRegexCompiler Emit = { Pattern, Result->Program.data() };
    Emit.EmitByte(0x9c);
    if (Emit.CompileExpression(false, Flags) == GNullNode)
        return nullptr;
    const unsigned char* Program = Result->Program.data();
    if (Program[NextRegexNode(Program, 1)] == 0)
    {
        int Node = 4;
        if (Program[Node] == 8)
            Result->StartCharacter = Program[Node + 3];
        else if (Program[Node] == 1)
            Result->bAnchored = true;
        if ((Flags & 4) != 0)
        {
            while (Node != GNullNode)
            {
                if (Program[Node] == 8)
                {
                    const int Length = (int)std::strlen((const char*)(Program + Node + 3));
                    if (Length >= Result->MustLength)
                    {
                        Result->MustOffset = Node + 3;
                        Result->MustLength = Length;
                    }
                }
                Node = NextRegexNode(Program, Node);
            }
        }
    }
    return Result;
}

// 06ed0540: mandatory-substring precheck, anchored/start-character/general search.
bool FMaterialRegex::Search(const FMaterialRegex* Expression, const char* Text)
{
    if (Expression == nullptr || Text == nullptr || Expression->Program[0] != 0x9c)
        return false;
    if (Expression->MustOffset != -1)
    {
        const char* Must = (const char*)(Expression->Program.data() + Expression->MustOffset);
        const char* Found = std::strchr(Text, *Must);
        while (Found != nullptr && std::strncmp(Found, Must, Expression->MustLength) != 0)
            Found = std::strchr(Found + 1, *Must);
        if (Found == nullptr)
            return false;
    }
    FRegexExecutor Executor = { Expression->Program.data(), Text, Text };
    if (Expression->bAnchored)
        return Executor.Try(Text);
    if (Expression->StartCharacter != 0)
    {
        const char* Found = std::strchr(Text, Expression->StartCharacter);
        while (Found != nullptr)
        {
            if (Executor.Try(Found))
                return true;
            Found = std::strchr(Found + 1, Expression->StartCharacter);
        }
        return false;
    }
    do
    {
        if (Executor.Try(Text))
            return true;
    } while (*Text++ != '\0');
    return false;
}

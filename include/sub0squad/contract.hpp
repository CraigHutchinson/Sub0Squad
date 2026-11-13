#pragma once
/// @file contract.hpp
/// The functions an item's file MUST define (catalogue row 59). The harness already knows them: its own stub of the file (the scaffold every workspace
/// starts from) defines exactly the functions the tests link against. So the list of a fork's sections starts from the contract, not from what the model
/// happened to declare: every contract function is a mandatory section with a generated stub, and the model adds only helpers. The same list decides
/// whether an `undefined reference` is mechanically mendable (mends.hpp: it is, when the name is a contract function).
/// Pure text functions: the caller supplies the stub and the headers.

#include <string>
#include <vector>

namespace sub0squad {

struct ContractFunction
{
    std::string name;        ///< the function's own name (`evaluate`)
    std::string scope;       ///< the namespace the stub defines it in (`calc`), "" for none and for Python
    std::string signature;   ///< as a definition has it, with parameter NAMES and no default arguments: the header's declaration when one exists, else the stub's line
};

/// The contract of the file at `path`: every non-static, non-private function its scaffold stub `stubText` defines (C++: not `static`, not `main`;
/// Python: module-level, not starting with `_`). `headers` are the scaffold's header texts: a declaration of the same name there supplies the signature
/// (the stub's own may leave parameters unnamed).
[[nodiscard]] std::vector<ContractFunction> contractFunctions(const std::string& path, const std::string& stubText, const std::vector<std::string>& headers);

/// A declared skeleton (the primer's reply for one file: structure and `@@ signature` lines) with every contract function declared: a `@@` line of that
/// name gets the contract's signature, a missing one is added (inside the function's namespace when the skeleton opens it). A contract function the
/// skeleton writes out itself (with its body) is left as written.
[[nodiscard]] std::string withContractDeclarations(const std::string& declared, const std::string& path, const std::vector<ContractFunction>& contract);

/// The contract functions `text` does not define (by name), in contract order.
[[nodiscard]] std::vector<ContractFunction> undefinedContract(const std::string& text, const std::string& path, const std::vector<ContractFunction>& contract);

/// `text` with a stub definition of `signature` added (a body that only raises `not implemented: NAME`, as a skeleton's stubs do), inside namespace `scope`
/// when the file opens it, else at the end. `functions` (complete function texts, may be empty) are put back just before it: sections an earlier
/// assembly removed because nobody called them yet.
[[nodiscard]] std::string withStubFunction(const std::string& text, const std::string& path, const std::string& signature, const std::string& scope, const std::vector<std::string>& functions = {});

/// The declaration of `name` in `text` as a signature (`static double helper(int x)`): its prototype line without the `;`. "" when there is none.
[[nodiscard]] std::string prototypeOf(const std::string& text, const std::string& name);

}  // namespace sub0squad

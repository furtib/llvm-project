set(LLVM_LINK_COMPONENTS
  support
  FrontendOpenMP
  )

find_package(Boost REQUIRED COMPONENTS regex)
include_directories(${RE2_INCLUDE_DIRS})

set(REGEX_CHECK_SOURCE "RegexCheck.cpp")

if (MSVC)
  # Windows
  add_library(RegexCheckObj OBJECT RegexCheck.cpp)
  target_compile_options(RegexCheckObj PRIVATE "/EHsc")
 
  target_include_directories(RegexCheckObj PRIVATE
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
  )
  find_package(re2 CONFIG REQUIRED)
  target_link_libraries(RegexCheckObj PRIVATE re2::re2 Boost::regex)
  set(REGEX_CHECK_SOURCE "$<TARGET_OBJECTS:RegexCheckObj>")
   
else()
  # Linux
  set_source_files_properties(RegexCheck.cpp PROPERTIES COMPILE_FLAGS "-fexceptions")
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(RE2 REQUIRED re2)
endif()

add_clang_library(clangTidyBugproneModule STATIC
  PARTIAL_SOURCES_INTENDED
  ArgumentCommentCheck.cpp
  AssertSideEffectCheck.cpp
  AssignmentInIfConditionCheck.cpp
  BadSignalToKillThreadCheck.cpp
  BitwisePointerCastCheck.cpp
  BoolPointerImplicitConversionCheck.cpp
  BranchCloneCheck.cpp
  BugproneTidyModule.cpp
  CapturingThisInMemberVariableCheck.cpp
  CastingThroughVoidCheck.cpp
  ChainedComparisonCheck.cpp
  ComparePointerToMemberVirtualFunctionCheck.cpp
  CopyConstructorInitCheck.cpp
  CrtpConstructorAccessibilityCheck.cpp
  DanglingHandleCheck.cpp
  DynamicStaticInitializersCheck.cpp
  EasilySwappableParametersCheck.cpp
  EmptyCatchCheck.cpp
  ExceptionEscapeCheck.cpp
  FoldInitTypeCheck.cpp
  ForwardDeclarationNamespaceCheck.cpp
  ForwardingReferenceOverloadCheck.cpp
  ImplicitWideningOfMultiplicationResultCheck.cpp
  InaccurateEraseCheck.cpp
  IncorrectEnableIfCheck.cpp
  IncorrectEnableSharedFromThisCheck.cpp
  InvalidEnumDefaultInitializationCheck.cpp
  UnintendedCharOstreamOutputCheck.cpp
  ReturnConstRefFromParameterCheck.cpp
  SuspiciousStringviewDataUsageCheck.cpp
  ${REGEX_CHECK_SOURCE}
  SwitchMissingDefaultCaseCheck.cpp
  IncDecInConditionsCheck.cpp
  IncorrectEnableIfCheck.cpp
  IncorrectRoundingsCheck.cpp
  InfiniteLoopCheck.cpp
  IntegerDivisionCheck.cpp
  LambdaFunctionNameCheck.cpp
  MacroParenthesesCheck.cpp
  MacroRepeatedSideEffectsCheck.cpp
  MisleadingSetterOfReferenceCheck.cpp
  MisplacedOperatorInStrlenInAllocCheck.cpp
  MisplacedPointerArithmeticInAllocCheck.cpp
  MisplacedWideningCastCheck.cpp
  MoveForwardingReferenceCheck.cpp
  MultiLevelImplicitPointerConversionCheck.cpp
  MultipleNewInOneExpressionCheck.cpp
  MultipleStatementMacroCheck.cpp
  NarrowingConversionsCheck.cpp
  NoEscapeCheck.cpp
  NonZeroEnumToBoolConversionCheck.cpp
  NondeterministicPointerIterationOrderCheck.cpp
  NotNullTerminatedResultCheck.cpp
  OptionalValueConversionCheck.cpp
  ParentVirtualCallCheck.cpp
  PointerArithmeticOnPolymorphicObjectCheck.cpp
  PosixReturnCheck.cpp
  RedundantBranchConditionCheck.cpp
  ReservedIdentifierCheck.cpp
  ReturnConstRefFromParameterCheck.cpp
  SharedPtrArrayMismatchCheck.cpp
  SignalHandlerCheck.cpp
  SignedCharMisuseCheck.cpp
  SizeofContainerCheck.cpp
  SizeofExpressionCheck.cpp
  SmartPtrArrayMismatchCheck.cpp
  SpuriouslyWakeUpFunctionsCheck.cpp
  StandaloneEmptyCheck.cpp
  StringConstructorCheck.cpp
  StringIntegerAssignmentCheck.cpp
  StringLiteralWithEmbeddedNulCheck.cpp
  StringviewNullptrCheck.cpp
  SuspiciousEnumUsageCheck.cpp
  SuspiciousIncludeCheck.cpp
  SuspiciousMemoryComparisonCheck.cpp
  SuspiciousMemsetUsageCheck.cpp
  SuspiciousMissingCommaCheck.cpp
  SuspiciousReallocUsageCheck.cpp
  SuspiciousSemicolonCheck.cpp
  SuspiciousStringCompareCheck.cpp
  SuspiciousStringviewDataUsageCheck.cpp
  SwappedArgumentsCheck.cpp
  SwitchMissingDefaultCaseCheck.cpp
  TaggedUnionMemberCountCheck.cpp
  TerminatingContinueCheck.cpp
  ThrowKeywordMissingCheck.cpp
  TooSmallLoopVariableCheck.cpp
  UncheckedOptionalAccessCheck.cpp
  UndefinedMemoryManipulationCheck.cpp
  UndelegatedConstructorCheck.cpp
  UnhandledExceptionAtNewCheck.cpp
  UnhandledSelfAssignmentCheck.cpp
  UniquePtrArrayMismatchCheck.cpp
  UnsafeFunctionsCheck.cpp
  UnusedLocalNonTrivialVariableCheck.cpp
  UnusedRaiiCheck.cpp
  UnusedReturnValueCheck.cpp
  UseAfterMoveCheck.cpp
  VirtualNearMissCheck.cpp

  LINK_LIBS
  clangTidy
  clangTidyUtils
  Boost::regex
  $<$<BOOL:${MSVC}>:re2::re2>
  ${RE2_LIBRARIES}

  DEPENDS
  omp_gen
  ClangDriverOptions
  )

clang_target_link_libraries(clangTidyBugproneModule
  PRIVATE
  clangAnalysis
  clangAnalysisFlowSensitive
  clangAnalysisFlowSensitiveModels
  clangAST
  clangASTMatchers
  clangBasic
  clangLex
  clangSema
  clangTooling
  clangTransformer
  )

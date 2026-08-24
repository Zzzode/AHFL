; AHFL contract SMT-LIB 2 encoding (RFC 0017)
(set-logic QF_LIA)
(declare-const input__qty Int)
(declare-const output__total Int)
(declare-const input__price Int)
; contract formal::smt_encoding::CalcAgent requires[0]
(assert (> input__qty 0))
; contract formal::smt_encoding::CalcAgent ensures[1]
(assert (= output__total (* input__qty input__price)))
(check-sat)

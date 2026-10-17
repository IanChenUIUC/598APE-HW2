(optimize-level 3)
(run-cp0 (lambda (cp0 x) x))
(enable-type-recovery #f)
(generate-interrupt-trap #f)

(define (feature k) (string->symbol (format "x~a" k)))

;; tree: flonum | (var k) | (op arg ...), op in add sub mul fdim abs sin cos log sq sqrt
(define (kernel-source tree min-val)
  (define used '())
  (define (tr e)
    (if (flonum? e) e
        (let ([a (lambda () (tr (cadr e)))] [b (lambda () (tr (caddr e)))])
          (case (car e)
            [(var) (let ([k (cadr e)])
                     (unless (memv k used) (set! used (cons k used)))
                     (feature k))]
            [(add) `(fl+ ,(a) ,(b))]
            [(sub) `(fl- ,(a) ,(b))]
            [(mul) `(fl* ,(a) ,(b))]
            [(fdim) `(let ([d (fl- ,(a) ,(b))]) (if (fl> d 0.0) d 0.0))]
            [(abs) `(flabs ,(a))]
            [(sin) `(flsin ,(a))]
            [(cos) `(flcos ,(a))]
            [(log) `(let ([v (flabs ,(a))]) (if (fl< v ,min-val) 0.0 (fllog v)))]
            [(sq) `(let ([v ,(a)]) (fl* v v))]
            [(sqrt) `(flsqrt (flabs ,(a)))]))))
  (let ([body (tr tree)])
    `(foreign-callable
      (lambda (data y n)
        (do ([i 0 (fx+ i 1)]) ((fx= i n))
          (let ,(map (lambda (k)
                       `[,(feature k) (foreign-ref 'float data (fx* 4 (fx+ (fx* ,k n) i)))])
                     (sort < used))
            (foreign-set! 'float y (fx* 4 i) ,body))))
      (uptr uptr size_t) void)))

(define (compile-kernel tree min-val)
  (let ([c (eval (kernel-source tree min-val))])
    (lock-object c)
    (foreign-callable-entry-point c)))

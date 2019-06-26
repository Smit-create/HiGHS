module highs_lp_solver
  interface
    function Highs_call (n, m, nz, cc, cl, cu, rl, ru, as, ai, av, cv, cd, rv, rd, cbs, rbs) result(s) bind (c, name='Highs_call')
      use iso_c_binding
      integer ( c_int ), VALUE :: n
      integer ( c_int ), VALUE :: m
      integer ( c_int ), VALUE :: nz
      real ( c_double ) :: cc(*)
      real ( c_double ) :: cl(*)
      real ( c_double ) :: cu(*)
      real ( c_double ) :: rl(*)
      real ( c_double ) :: ru(*)
      integer ( c_int ) :: as(*)
      integer ( c_int ) :: ai(*)
      real ( c_double ) :: av(*)
      real ( c_double ) :: cv(*)
      real ( c_double ) :: cd(*)
      real ( c_double ) :: rv(*)
      real ( c_double ) :: rd(*)
      integer ( c_int ) :: cbs(*)
      integer ( c_int ) :: rbs(*)
      integer ( c_int ) :: s
    end function Highs_call
  end interface
end module highs_lp_solver

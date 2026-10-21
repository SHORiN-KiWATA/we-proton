using System;
using System.Runtime.CompilerServices;
using System.Threading;

// Each AppDomain has a cold trampoline cache; race construction against its
// first invocations. This is a stress test, not a deterministic race detector.
class DelegateCachePublication
{
    const int WorkerCount = 16;
    const int Iterations = 4096;
    static int staticCalls;

    sealed class Target<T>
    {
        public int Calls;

        [MethodImpl(MethodImplOptions.NoInlining)]
        void Invoke()
        {
            Calls++;
        }

        [MethodImpl(MethodImplOptions.NoInlining)]
        public Action Create()
        {
            return Invoke;
        }
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    static void InvokeStatic()
    {
        Interlocked.Increment(ref staticCalls);
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    static Action CreateStatic()
    {
        return InvokeStatic;
    }

    static void RunRound()
    {
        Exception failure = null;
        staticCalls = 0;
        using (var ready = new CountdownEvent(WorkerCount))
        using (var start = new ManualResetEvent(false))
        {
            var workers = new Thread[WorkerCount];
            for (int i = 0; i < workers.Length; i++)
            {
                workers[i] = new Thread(() =>
                {
                    var valueTarget = new Target<int>();
                    var sharedTarget = new Target<string>();
                    ready.Signal();
                    start.WaitOne();
                    try
                    {
                        for (int j = 0; j < Iterations; j++)
                        {
                            valueTarget.Create()();
                            sharedTarget.Create()();
                            CreateStatic()();
                        }
                        if (valueTarget.Calls != Iterations || sharedTarget.Calls != Iterations)
                            throw new Exception("A delegate invoked the wrong target.");
                    }
                    catch (Exception e)
                    {
                        Interlocked.CompareExchange(ref failure, e, null);
                    }
                });
                workers[i].Start();
            }
            ready.Wait();
            start.Set();
            foreach (var worker in workers)
                worker.Join();
        }
        if (failure != null)
            throw new Exception("Delegate cache stress failed.", failure);
        if (staticCalls != WorkerCount * Iterations)
            throw new Exception("A static delegate invocation was lost.");
    }

    static int Main(string[] args)
    {
        int rounds = args.Length == 0 ? 64 : int.Parse(args[0]);
        if (rounds < 1)
            throw new ArgumentOutOfRangeException("rounds");
        for (int i = 0; i < rounds; i++)
        {
            var domain = AppDomain.CreateDomain("delegate-cache-" + i);
            try
            {
                domain.DoCallBack(RunRound);
            }
            finally
            {
                AppDomain.Unload(domain);
            }
        }
        Console.WriteLine("Delegate cache publication stress passed ({0} rounds).", rounds);
        return 0;
    }
}

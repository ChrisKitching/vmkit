/*
 *  This file is part of the Jikes RVM project (http://jikesrvm.org).
 *
 *  This file is licensed to You under the Common Public License (CPL);
 *  You may not use this file except in compliance with the License. You
 *  may obtain a copy of the License at
 *
 *      http://www.opensource.org/licenses/cpl1.0.php
 *
 *  See the COPYRIGHT.txt file distributed with this work for information
 *  regarding copyright ownership.
 */
package org.vmmagic.pragma;

import java.lang.annotation.Retention;
import java.lang.annotation.Target;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.ElementType;
import org.vmmagic.Pragma;

/**
 * By default all Java code is interruptible, that is scheduling or garbage
 * collection may occur at points within the code. Code can be marked as
 * {@link Unpreemptible} or {@link Uninterruptible}, that instructs the JVM to
 * avoid garbage collection and thread scheduling. The {@link Uninterruptible}
 * annotation disallows any operation that may cause garbage collection or
 * thread scheduling, for example memory allocation. The {@link Unpreemptible}
 * annotation doesn't disallow operations that can cause garbage collection or
 * scheduling, but instructs the JVM to avoid inserting such operations during a
 * block of code.
 *
 * In the internals of a VM most code wants to be {@link Uninterruptible}.
 * However, code involved in scheduling and locking will cause context switches,
 * and creating exception objects may trigger garbage collection, this code is
 * therefore {@link Unpreemptible}.
 *
 * This pragma is used to declare that a particular method is interruptible. It
 * is used to override the class-wide pragma {@link Uninterruptible}.
 */
@Retention(RetentionPolicy.RUNTIME)
@Target(ElementType.METHOD)
@Pragma
public @interface Interruptible {
  /**
   * @return Explanation of why code needs to be interruptible
   */
  String value() default "";
}
